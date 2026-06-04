#!/usr/bin/env python3
"""Sincroniza la version de la app tomando max(CMake, ChangeLog)."""

from __future__ import annotations

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
    path.write_text(content, encoding="utf-8", newline=newline)


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
    changelog_content = _read_text(CHANGELOG_MD)
    cmake_content = _read_text(CMAKE_FILE)

    cmake_version = _extract_cmake_project_version(cmake_content)
    changelog_version = _extract_changelog_version(changelog_content)
    resolved_version = _max_version(cmake_version, changelog_version)

    new_changelog = _replace_changelog_version(changelog_content, resolved_version)
    new_cmake = _replace_cmake_project_version(cmake_content, resolved_version)

    _write_text(CHANGELOG_MD, new_changelog, changelog_content)
    _write_text(CMAKE_FILE, new_cmake, cmake_content)
    _write_text(VERSION_FILE, f"{resolved_version}\n", _read_text(VERSION_FILE))

    print(f"[sync_version] CMake version:    {cmake_version}")
    print(f"[sync_version] ChangeLog version:{changelog_version}")
    print(f"[sync_version] Resolved version: {resolved_version}")
    print("[sync_version] Files synced: ChangeLog.md, VERSION, CMakeLists.txt")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pylint: disable=broad-except
        print(f"[sync_version] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
