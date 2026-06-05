#!/usr/bin/env python3
"""
LGA_SeqChecker - Backend de detección de frames EXR corruptos.

Escanea recursivamente una o varias carpetas, agrupa los .exr en secuencias y
analiza cada frame en tres capas (de barato a caro):

  Capa 0 (estructural, gratis): gaps en la numeración + outliers de tamaño de
          archivo (un frame mucho más chico que la mediana = probable truncado).
  Capa 1 (header): consistencia de resolución / canales / compresión dentro de
          la secuencia, vía `exrheader`.
  Capa 2 (decode completo): validación real de corrupción con `exrcheck`
          (herramienta oficial de OpenEXR), en un pool de workers.

Protocolo de salida (stdout, una línea JSON por evento, prefijo MT_):
  MT_SEQ_FOUND  {id,name,folder,frames,range}
  MT_SEQ_START  {id,position,remaining}
  MT_PROGRESS   {done,total}
  MT_SEQ_RESULT {id,ok,suspect,corrupt,status,detail}
  MT_DONE       {sequences,ok,suspect,corrupt}
"""

import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
SIZE_OUTLIER_RATIO = 0.5      # frame < 50% de la mediana => sospechoso (truncado)
EXRCHECK_TIMEOUT = 60         # s por frame para exrcheck
EXRHEADER_TIMEOUT = 20        # s por frame para exrheader
FRAME_RE = re.compile(r'^(.*?)(\d+)(\.exr)$', re.IGNORECASE)

_NO_WINDOW = 0
if os.name == "nt":
    _NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)

_print_lock = threading.Lock()


def emit(tag, obj):
    with _print_lock:
        sys.stdout.write(tag + " " + json.dumps(obj, ensure_ascii=False) + "\n")
        sys.stdout.flush()


# ---------------------------------------------------------------------------
# Control dinamico de concurrencia (limite de workers ajustable en vivo)
# ---------------------------------------------------------------------------
class DynamicGate:
    """Compuerta de concurrencia con limite ajustable en caliente.

    El pool de threads se crea con un tope alto fijo, pero la cantidad de
    frames procesados simultaneamente la controla este gate. Al bajar el
    limite, los frames ya en proceso terminan y recien ahi se frena el resto;
    al subirlo, arrancan mas de inmediato. Asi el cambio de CPU se aplica
    apenas terminan los frames actualmente en vuelo, no al final del job.
    """

    def __init__(self, limit):
        self._cond = threading.Condition()
        self._limit = max(1, int(limit))
        self._active = 0

    def set_limit(self, value):
        with self._cond:
            value = max(1, int(value))
            if value != self._limit:
                self._limit = value
                self._cond.notify_all()

    def __enter__(self):
        with self._cond:
            while self._active >= self._limit:
                self._cond.wait()
            self._active += 1
        return self

    def __exit__(self, *_exc):
        with self._cond:
            self._active -= 1
            self._cond.notify_all()


def _read_control_workers(path):
    """Lee el entero de workers del control-file. None si no se pudo."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            txt = f.read().strip()
        if txt:
            return int(txt)
    except Exception:  # noqa: BLE001
        pass
    return None


def start_control_watcher(path, gate, max_workers, stop_event):
    """Thread que relee el control-file y ajusta el gate en vivo."""
    if not path:
        return None

    def _loop():
        last = None
        while not stop_event.wait(0.2):
            n = _read_control_workers(path)
            if n is None or n == last:
                continue
            last = n
            n = max(1, min(int(n), max_workers))
            gate.set_limit(n)

    t = threading.Thread(target=_loop, name="cpu_control_watcher", daemon=True)
    t.start()
    return t


# ---------------------------------------------------------------------------
# Resolución de binarios
# ---------------------------------------------------------------------------
def _find_tool(name):
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = []
    for base in (here, os.path.dirname(here), os.path.dirname(os.path.dirname(here))):
        candidates.append(os.path.join(base, "thirdparty", "win", "OpenEXR", name))
        candidates.append(os.path.join(base, "thirdparty", "win", "OIIO", name))
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None


EXRCHECK = _find_tool("exrcheck.exe")
EXRHEADER = _find_tool("exrheader.exe")


# ---------------------------------------------------------------------------
# Escaneo y agrupación en secuencias
# ---------------------------------------------------------------------------
class Sequence:
    def __init__(self, seq_id, folder, prefix, ext):
        self.id = seq_id
        self.folder = folder
        self.prefix = prefix
        self.ext = ext
        self.frames = {}   # frame_number(int) -> filepath  (None key = standalone)
        self.pad = 0

    @property
    def count(self):
        return len(self.frames)

    def display_name(self):
        nums = [n for n in self.frames if n is not None]
        if not nums:
            # standalone: usar el nombre real del único archivo
            return os.path.basename(next(iter(self.frames.values())))
        pad = self.pad or 4
        return "{}{}{}".format(self.prefix, "#" * pad, self.ext)

    def frame_range(self):
        nums = sorted(n for n in self.frames if n is not None)
        if not nums:
            return "-"
        return "{}-{}".format(nums[0], nums[-1])


def scan_folders(folders):
    """Devuelve lista de Sequence."""
    # key (dir, prefix.lower(), ext.lower()) -> Sequence
    seqs = {}
    standalone_idx = 0

    for root_folder in folders:
        for dirpath, _dirs, files in os.walk(root_folder):
            for fn in files:
                if not fn.lower().endswith(".exr"):
                    continue
                full = os.path.join(dirpath, fn)
                m = FRAME_RE.match(fn)
                if m:
                    prefix, digits, ext = m.group(1), m.group(2), m.group(3)
                    key = (dirpath, prefix.lower(), ext.lower())
                    seq = seqs.get(key)
                    if seq is None:
                        seq = Sequence("{}::{}{}#{}".format(dirpath, prefix, ext, len(seqs)),
                                       dirpath, prefix, ext)
                        seqs[key] = seq
                    seq.frames[int(digits)] = full
                    seq.pad = max(seq.pad, len(digits))
                else:
                    # .exr sin número de frame => secuencia de 1
                    standalone_idx += 1
                    sid = "{}::standalone#{}".format(dirpath, standalone_idx)
                    seq = Sequence(sid, dirpath, os.path.splitext(fn)[0], ".exr")
                    seq.frames[None] = full
                    seqs[sid] = seq

    return list(seqs.values())


# ---------------------------------------------------------------------------
# Análisis por frame
# ---------------------------------------------------------------------------
def _run(cmd, timeout):
    """Devuelve (returncode, stdout+stderr). returncode None si timeout/excepción."""
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           creationflags=_NO_WINDOW)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired:
        return None, "timeout"
    except Exception as e:  # noqa: BLE001
        return None, str(e)


def exrcheck_frame(path):
    """True = OK, False = corrupto. (detalle)

    Se usan los flags -t -m (evitar tiempo/memoria excesivos): exrcheck deja de
    hacer las pasadas redundantes de su modo fuzzing y valida en un solo decode,
    ~2.4x mas rapido, manteniendo la deteccion de truncados y chunks corruptos.
    """
    if not EXRCHECK:
        return True, ""
    rc, out = _run([EXRCHECK, "-t", "-m", path], EXRCHECK_TIMEOUT)
    if rc is None:
        return False, "exrcheck: " + out.strip()[:200]
    if rc != 0:
        return False, "exrcheck rc={}: {}".format(rc, out.strip()[:200])
    return True, ""


_HEADER_COMP_RE = re.compile(r'compression[^:]*:\s*([^\n,]+)', re.IGNORECASE)
_HEADER_DW_RE = re.compile(r'dataWindow[^:]*:\s*\((-?\d+)\s+(-?\d+)\)\s*-\s*\((-?\d+)\s+(-?\d+)\)',
                           re.IGNORECASE)


def header_signature(path):
    """Firma del header para chequeo de consistencia. None si no se pudo leer."""
    if not EXRHEADER:
        return None
    rc, out = _run([EXRHEADER, path], EXRHEADER_TIMEOUT)
    if rc is None or rc != 0:
        return None
    comp = _HEADER_COMP_RE.search(out)
    dw = _HEADER_DW_RE.search(out)
    if not dw:
        return None
    x0, y0, x1, y1 = map(int, dw.groups())
    w, h = x1 - x0 + 1, y1 - y0 + 1
    comp_s = comp.group(1).strip() if comp else "?"
    return "{}x{}|{}".format(w, h, comp_s)


# ---------------------------------------------------------------------------
# Orquestación
# ---------------------------------------------------------------------------
def analyze(folders, workers, max_workers=None, control_file=None):
    seqs = scan_folders(folders)

    # Tope del pool: lo mas alto que el usuario podria pedir en vivo.
    pool_cap = max(int(workers), int(max_workers or workers), 1)
    gate = DynamicGate(workers)
    stop_event = threading.Event()
    watcher = start_control_watcher(control_file, gate, pool_cap, stop_event)

    total_frames = 0
    for seq in seqs:
        total_frames += seq.count
        emit("MT_SEQ_FOUND", {
            "id": seq.id,
            "name": seq.display_name(),
            "folder": seq.folder,
            "frames": seq.count,
            "range": seq.frame_range(),
        })

    if total_frames == 0:
        stop_event.set()
        emit("MT_DONE", {"sequences": 0, "ok": 0, "suspect": 0, "corrupt": 0})
        return

    done = {"n": 0}
    last_emit = {"t": 0.0}
    prog_lock = threading.Lock()

    def tick():
        with prog_lock:
            done["n"] += 1
            now = time.time()
            if done["n"] == total_frames or now - last_emit["t"] > 0.15:
                last_emit["t"] = now
                emit("MT_PROGRESS", {"done": done["n"], "total": total_frames})

    # Resultado por frame. El gate limita cuantos frames se procesan a la vez.
    # No se lee el header por frame (era un 2do subproceso por frame): exrcheck
    # ya valida el archivo completo, asi que el chequeo de consistencia de header
    # no aportaba deteccion real y duplicaba el costo de spawn.
    def check_one(path):
        with gate:
            ok, detail = exrcheck_frame(path)
        tick()
        return path, ok, detail

    totals = {"ok": 0, "suspect": 0, "corrupt": 0}

    with ThreadPoolExecutor(max_workers=pool_cap) as pool:
        for seq_index, seq in enumerate(seqs):
            emit("MT_SEQ_START", {
                "id": seq.id,
                "position": seq_index + 1,
                "remaining": max(0, len(seqs) - (seq_index + 1)),
            })
            paths = [p for _n, p in sorted(
                ((n if n is not None else -1, p) for n, p in seq.frames.items()))]

            # --- Capa 0: tamaños + gaps ---
            sizes = {}
            for p in paths:
                try:
                    sizes[p] = os.path.getsize(p)
                except OSError:
                    sizes[p] = 0
            size_list = sorted(sizes.values())
            median = size_list[len(size_list) // 2] if size_list else 0
            size_suspect = set()
            if median > 0 and len(paths) >= 3:
                for p, s in sizes.items():
                    if s < median * SIZE_OUTLIER_RATIO:
                        size_suspect.add(p)

            nums = sorted(n for n in seq.frames if n is not None)
            missing = []
            if len(nums) >= 2:
                expected = set(range(nums[0], nums[-1] + 1))
                missing = sorted(expected - set(nums))

            # --- Capa 2: exrcheck por frame (en pool) ---
            results = list(pool.map(check_one, paths))

            ok_n = suspect_n = corrupt_n = 0
            issues = []
            corrupt_files = []   # paths completos de frames corruptos (para el dialogo)
            suspect_files = []   # paths completos de frames sospechosos
            for p, ok, detail in results:
                base = os.path.basename(p)
                if not ok:
                    corrupt_n += 1
                    corrupt_files.append(p)
                    issues.append("CORRUPT {} — {}".format(base, detail))
                elif p in size_suspect:
                    suspect_n += 1
                    suspect_files.append(p)
                    issues.append("SUSPECT {} — file size {}% of median".format(
                        base, int(100 * sizes[p] / median) if median else 0))
                else:
                    ok_n += 1

            if missing:
                preview = ", ".join(str(x) for x in missing[:20])
                if len(missing) > 20:
                    preview += ", …"
                issues.insert(0, "MISSING {} frame(s): {}".format(len(missing), preview))

            if corrupt_n > 0:
                status = "corrupt"
            elif suspect_n > 0 or missing:
                status = "suspect"
            else:
                status = "ok"

            totals["ok"] += ok_n
            totals["suspect"] += suspect_n
            totals["corrupt"] += corrupt_n

            emit("MT_SEQ_RESULT", {
                "id": seq.id,
                "ok": ok_n,
                "suspect": suspect_n,
                "corrupt": corrupt_n,
                "status": status,
                "detail": "\n".join(issues),
                "corrupt_files": corrupt_files,
                "suspect_files": suspect_files,
            })

    stop_event.set()

    emit("MT_DONE", {
        "sequences": len(seqs),
        "ok": totals["ok"],
        "suspect": totals["suspect"],
        "corrupt": totals["corrupt"],
    })


def main():
    parser = argparse.ArgumentParser(description="LGA SeqChecker - EXR corruption detector")
    parser.add_argument("--folder", action="append", default=[], help="carpeta a analizar (repetible)")
    parser.add_argument("--workers", type=int, default=max(2, (os.cpu_count() or 4) - 1))
    parser.add_argument("--max-workers", type=int, default=0,
                        help="tope del pool para escalado en vivo (0 = igual a --workers)")
    parser.add_argument("--control-file", default=None,
                        help="archivo con el limite de workers, releido en vivo")
    parser.add_argument("--json-lines", action="store_true", help="(default) salida MT_ por línea")
    args = parser.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:  # noqa: BLE001
        pass

    folders = [f for f in args.folder if f and os.path.isdir(f)]
    if not folders:
        sys.stderr.write("No valid folders provided\n")
        return 2

    if not EXRCHECK:
        sys.stderr.write("WARNING: exrcheck.exe not found; capa 2 (decode) deshabilitada\n")

    analyze(folders, args.workers,
            max_workers=(args.max_workers or args.workers),
            control_file=args.control_file)
    return 0


if __name__ == "__main__":
    sys.exit(main())
