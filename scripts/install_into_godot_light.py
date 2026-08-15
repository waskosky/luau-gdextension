#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description="Install the Luau add-on into a Godot-light checkout")
    parser.add_argument("--godot-project", required=True, type=Path)
    parser.add_argument("--require-binaries", action="store_true")
    args = parser.parse_args()
    source = ROOT / "addons/luau_gdextension"
    destination = args.godot_project.resolve() / "addons/luau_gdextension"
    binaries = [item for item in (source / "bin").rglob("*") if item.is_file()]
    if args.require_binaries and not binaries:
        raise SystemExit("no native libraries have been built")
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns("*.pyc", "__pycache__"))
    print(destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
