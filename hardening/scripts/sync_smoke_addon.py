#!/usr/bin/env python3
"""Copy the built addon into the standalone smoke and benchmark projects."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


DEBUG_BINARY_ALIASES = {
    "libgdluau.linux.x86_64.so": "libgdluau.linux.x86_64.debug.so",
    "libgdluau.darwin.arm64.dylib": "libgdluau.darwin.arm64.debug.dylib",
    "gdluau.windows.amd64.dll": "gdluau.windows.amd64.debug.dll",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    repo = args.repo.resolve()
    source = repo / "addons" / "luau_gdextension"
    if not source.is_dir():
        raise SystemExit(f"Addon directory not found: {source}")

    for project in [repo / "hardening" / "smoke", repo / "hardening" / "benchmark"]:
        destination = project / "addons" / "luau_gdextension"
        if destination.exists():
            shutil.rmtree(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, destination)

        # Official Godot editor binaries advertise the `debug` feature even
        # when exercising a Release GDExtension. Give standalone smoke and
        # benchmark projects a debug-named alias for the Release artifact so
        # the copied descriptor resolves on every supported host platform.
        binary_dir = destination / "bin"
        for release_name, debug_name in DEBUG_BINARY_ALIASES.items():
            release_binary = binary_dir / release_name
            if release_binary.is_file():
                shutil.copy2(release_binary, binary_dir / debug_name)

        # Standalone headless runs do not perform an editor filesystem scan,
        # so seed the same extension list Godot would otherwise generate.
        # This ensures native classes exist before the preloaded wrappers parse.
        godot_cache = project / ".godot"
        godot_cache.mkdir(parents=True, exist_ok=True)
        (godot_cache / "extension_list.cfg").write_text(
            "res://addons/luau_gdextension/luau.gdextension\n",
            encoding="utf-8",
        )

        print(f"Synced {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
