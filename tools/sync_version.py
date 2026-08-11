#!/usr/bin/env python3
"""Sincroniza la version de la app tomando max(CMake, ChangeLog).

Uso
---
    python tools/sync_version.py            # propaga
    python tools/sync_version.py --check    # NO escribe; sale 1 si algo difiere

El `--check` existe porque el wrapper acepta cualquier flag y se lo pasa tal
cual: antes el script lo ignoraba y sincronizaba igual, asi que
`sync_version.bat --check` --que parece una verificacion de solo lectura--
**bumpeaba la version y reescribia el CMakeLists**. Ya paso en LinkRedirector:
una auditoria lo corrio esperando algo de solo lectura y le escribio archivos.
Mismo flag y mismo contrato que LinkRedirector y LGA_RepoTools. OJO: en
FileManagerS3 y PipeSync el flag se llama `--check-only` y tiene otro contrato
(tienen un codigo 2 que sale solo con --allow-changelog-ahead-local-test); ahi `--check`
funciona nada mas que por la abreviacion automatica de argparse. No asumir que
el mismo comando significa lo mismo en los cinco repos.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
CHANGELOG_MD = ROOT_DIR / "docs" / "ChangeLog.md"
VERSION_FILE = ROOT_DIR / "VERSION"
CMAKE_FILE = ROOT_DIR / "CMakeLists.txt"


def _parse_version(version: str) -> tuple[int, ...]:
    return tuple(int(chunk) for chunk in version.strip().split("."))


def _max_version(left: str, right: str) -> str:
    left_key = _parse_version(left)
    right_key = _parse_version(right)
    max_len = max(len(left_key), len(right_key))
    left_key = left_key + (0,) * (max_len - len(left_key))
    right_key = right_key + (0,) * (max_len - len(right_key))
    return left if left_key >= right_key else right


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _write_text(path: Path, content: str, original_content: str | None = None) -> None:
    if original_content is not None and content == original_content:
        return

    newline = "\n"
    if original_content is not None and "\r\n" in original_content:
        newline = "\r\n"
    # `Path.write_text(newline=...)` recien existe en Python 3.10; con 3.9 (el python3
    # del sistema en macOS) tira "write_text() got an unexpected keyword argument
    # 'newline'" y el sync falla entero. `open()` acepta `newline` en todas las
    # versiones, asi que se escribe por ahi.
    with path.open("w", encoding="utf-8", newline=newline) as fh:
        fh.write(content)


def _extract_changelog_version(content: str) -> str:
    match = re.search(r"^\s*v([0-9]+(?:\.[0-9]+)+)\s*:", content, flags=re.MULTILINE)
    if not match:
        raise ValueError("No se pudo detectar la version en docs/ChangeLog.md")
    return match.group(1)


def _extract_cmake_project_version(content: str) -> str:
    match = re.search(
        r"project\(\s*SeqChecker\s+VERSION\s+([0-9]+(?:\.[0-9]+)+)",
        content,
    )
    if not match:
        raise ValueError("No se pudo detectar project(... VERSION ...) en CMakeLists.txt")
    return match.group(1)


def _replace_changelog_version(content: str, new_version: str) -> str:
    updated, count = re.subn(
        r"(^\s*v)([0-9]+(?:\.[0-9]+)+)(\s*:)",
        lambda match: f"{match.group(1)}{new_version}{match.group(3)}",
        content,
        count=1,
        flags=re.MULTILINE,
    )
    if count == 0:
        raise ValueError("No se pudo actualizar la version en docs/ChangeLog.md")
    return updated


def _replace_cmake_project_version(content: str, new_version: str) -> str:
    updated, count = re.subn(
        r"(project\(\s*SeqChecker\s+VERSION\s+)([0-9]+(?:\.[0-9]+)+)",
        lambda match: f"{match.group(1)}{new_version}",
        content,
        count=1,
    )
    if count == 0:
        raise ValueError("No se pudo actualizar project(... VERSION ...) en CMakeLists.txt")
    return updated


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Sincroniza la version tomando max(CMakeLists, ChangeLog)."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="No escribe nada; sale 1 si alguna superficie difiere de la resuelta.",
    )
    args = parser.parse_args()

    changelog_content = _read_text(CHANGELOG_MD)
    cmake_content = _read_text(CMAKE_FILE)

    cmake_version = _extract_cmake_project_version(cmake_content)
    changelog_version = _extract_changelog_version(changelog_content)
    resolved_version = _max_version(cmake_version, changelog_version)

    if args.check:
        version_file = _read_text(VERSION_FILE).strip() if VERSION_FILE.exists() else None
        desincronizados = []
        if cmake_version != resolved_version:
            desincronizados.append(f"CMakeLists.txt ({cmake_version})")
        if changelog_version != resolved_version:
            desincronizados.append(f"docs/ChangeLog.md ({changelog_version})")
        if version_file != resolved_version:
            desincronizados.append(f"VERSION ({version_file or 'falta'})")

        if desincronizados:
            print(f"[sync_version] ERROR: desincronizado contra {resolved_version}.")
            for item in desincronizados:
                print(f"    {item}")
            return 1
        print(f"[sync_version] OK: todo en {resolved_version}.")
        return 0

    new_changelog = _replace_changelog_version(changelog_content, resolved_version)
    new_cmake = _replace_cmake_project_version(cmake_content, resolved_version)

    _write_text(CHANGELOG_MD, new_changelog, changelog_content)
    _write_text(CMAKE_FILE, new_cmake, cmake_content)
    _write_text(VERSION_FILE, f"{resolved_version}\n", _read_text(VERSION_FILE))

    print(f"[sync_version] CMake version:     {cmake_version}")
    print(f"[sync_version] ChangeLog version: {changelog_version}")
    print(f"[sync_version] Resolved version:  {resolved_version}")
    print("[sync_version] Files synced: ChangeLog.md, VERSION, CMakeLists.txt")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pylint: disable=broad-except
        print(f"[sync_version] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
