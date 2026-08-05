#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release -j "${BUILD_JOBS:-2}"
ctest --preset macos-arm64-release
python3 hardening/scripts/sync_smoke_addon.py --repo "$ROOT"

GODOT_BIN="${GODOT_EXECUTABLE:-/Applications/Godot.app/Contents/MacOS/Godot}"
if [[ -x "$GODOT_BIN" ]]; then
  "$GODOT_BIN" --headless --path hardening/smoke
  "$GODOT_BIN" --headless --path hardening/benchmark
else
  printf '%s\n' 'Godot executable not found; native build/tests completed and smoke/benchmark were skipped.'
fi
