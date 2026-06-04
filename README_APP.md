# LGA SeqChecker

App Qt C++ (UI) + Python (backend) para detectar **frames EXR corruptos** en
secuencias. Ventana única: se arrastra una o varias carpetas y se analizan
recursivamente todas las secuencias `.exr` que se encuentren.

## Uso

1. Abrir la app.
2. Arrastrar carpeta(s) a la ventana → overlay de drop (1 zona).
3. Al soltar arranca el análisis automático. Los resultados aparecen por
   secuencia en la tabla, con conteo de frames OK / Suspect / Corrupt y estado.
4. Doble-click en una fila abre la carpeta de la secuencia. El tooltip de la
   fila lista los frames con problema.

## Detección (estrategia en capas)

El backend (`py_scr/LGA_SeqChecker.py`) corre, de barato a caro:

- **Capa 0 — estructural (gratis):** gaps en la numeración + outliers de tamaño
  de archivo (frame < 50% de la mediana ⇒ probable truncado). → `suspect`
- **Capa 1 — header (`exrheader`):** consistencia de resolución / compresión
  dentro de la secuencia; frames con header distinto al modo de la seq. → `suspect`
- **Capa 2 — decode completo (`exrcheck`):** validación real de corrupción con
  la herramienta oficial de OpenEXR, en un pool de workers. → `corrupt`

Estado de la secuencia: `corrupt` si hay ≥1 frame corrupto; `suspect` si hay
sospechosos o frames faltantes; si no, `ok`.

> Capa 3 opcional a futuro: NaN/Inf y frames negros con `oiiotool --stats`
> (requiere copiar `thirdparty/win/OIIO`, hoy no incluido).

## Protocolo C++ ↔ Python

El backend emite por stdout una línea JSON por evento (prefijo `MT_`), que
`MainWindow::onPythonLine()` parsea:

| Línea | Payload |
|---|---|
| `MT_SEQ_FOUND`  | `{id,name,folder,frames,range}` |
| `MT_PROGRESS`   | `{done,total}` |
| `MT_SEQ_RESULT` | `{id,ok,suspect,corrupt,status,detail}` |
| `MT_DONE`       | `{sequences,ok,suspect,corrupt}` |

## Estructura

- `src/ui/mainwindow/mainwindow.cpp` — ventana, overlay drag&drop (1 zona),
  tabla de resultados, wiring con `PythonRunner`.
- `src/main.cpp` — bootstrap, fuentes, logging.
- Infra reutilizada de MediaTools_v2 (verbatim, prefijo `mediatools/`):
  `PythonRunner`, `AppPathManager`, `debug_flags`, `LogRotation`.
- `py_scr/LGA_SeqChecker.py` — escaneo + detección.
- `thirdparty/win/OpenEXR/` — `exrcheck.exe`, `exrheader.exe`.
- `python_runtime/` — Python embebido.
- UI según `LGA_MediaTools_v2/docs/UI_STYLE_LGA_APPS.md` (tokens en
  `include/seqchecker/mainwindow.h`).

## Build (desarrollo)

```bat
compilar_dev.bat
```

Requiere Qt 6.5.3 mingw_64, Ninja y LLVM (lld), igual que MediaTools_v2.
El ejecutable queda en `build/SeqChecker.exe`.
