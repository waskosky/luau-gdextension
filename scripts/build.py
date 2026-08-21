#!/usr/bin/env python3
"""Configure and build the native extension for one desktop target."""
from __future__ import annotations

import argparse
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("template_debug", "template_release", "editor"), default="template_debug")
    parser.add_argument("--build-type", choices=("Debug", "Release", "RelWithDebInfo"), default="Release")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--arch", choices=("x86_64", "arm64"))
    parser.add_argument("--parallel", type=int, default=0)
    args = parser.parse_args()
    build_dir = (args.build_dir or ROOT / "build" / args.target).resolve()
    suffixes = {"Linux": "so", "Darwin": "dylib", "Windows": "dll"}
    system = platform.system()
    if system not in suffixes:
        raise SystemExit(f"unsupported desktop build host: {system}")
    configure = [
        "cmake", "-S", str(ROOT), "-B", str(build_dir),
        f"-DGODOTCPP_TARGET={args.target}",
        "-DGODOTCPP_API_VERSION=4.7",
        f"-DCMAKE_BUILD_TYPE={args.build_type}",
        f"-DLIBRARY_SUFFIX={suffixes[system]}",
        "-DBUILD_TESTING=OFF",
        f"-DGDLUAU_GODOT_TARGET_DEBUG={'ON' if args.target != 'template_release' else 'OFF'}",
    ]
    if args.arch:
        if system != "Darwin":
            parser.error("--arch is currently supported only for macOS builds")
        configure += [
            f"-DCMAKE_OSX_ARCHITECTURES={args.arch}",
            f"-DGDLUAU_ARCH_NAME={args.arch}",
        ]
    subprocess.run(configure, check=True)
    command = ["cmake", "--build", str(build_dir), "--target", "gdluau"]
    if system == "Windows":
        command += ["--config", args.build_type]
    if args.parallel > 0:
        command += ["--parallel", str(args.parallel)]
    else:
        command += ["--parallel"]
    subprocess.run(command, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
