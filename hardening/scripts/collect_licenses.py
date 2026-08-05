#!/usr/bin/env python3
"""Collect license texts from the root project and configured CMake dependencies."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def copy_first(candidates: list[Path], destination: Path, label: str) -> None:
    for candidate in candidates:
        if candidate.is_file():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(candidate, destination)
            print(f"{label}: {candidate} -> {destination}")
            return
    raise SystemExit(f"Could not locate {label}; checked: {', '.join(map(str, candidates))}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path, default=None)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build = args.build_dir.resolve() if args.build_dir else repo / "build"
    output = repo / "dist" / "licenses"

    copy_first([repo / "LICENSE"], output / "luau-gdextension-LICENSE.txt", "extension license")
    copy_first(
        [build / "_deps" / "luau-src" / "LICENSE.txt"],
        output / "Luau-LICENSE.txt",
        "Luau license",
    )
    copy_first(
        [build / "_deps" / "luau-src" / "lua_LICENSE.txt"],
        output / "Lua-LICENSE.txt",
        "Lua license",
    )
    copy_first(
        [
            build / "_deps" / "godotcpp-src" / "LICENSE.md",
            build / "_deps" / "godotcpp-src" / "LICENSE",
        ],
        output / "godot-cpp-LICENSE.txt",
        "godot-cpp license",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
