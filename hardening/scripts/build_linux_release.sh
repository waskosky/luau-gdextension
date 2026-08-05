#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

cmake --preset linux-x86_64-release
cmake --build --preset linux-x86_64-release -j "${BUILD_JOBS:-2}"
ctest --preset linux-x86_64-release
python3 hardening/scripts/sync_smoke_addon.py --repo "$ROOT"

GODOT_BIN="${GODOT_EXECUTABLE:-$(command -v godot || true)}"
if [[ -n "$GODOT_BIN" ]]; then
  "$GODOT_BIN" --headless --path hardening/smoke
  "$GODOT_BIN" --headless --path hardening/benchmark
else
  printf '%s\n' 'Godot executable not found; native build/tests completed and smoke/benchmark were skipped.'
fi
