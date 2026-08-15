from __future__ import annotations

import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_dependency_pins_are_exact() -> None:
    lock = json.loads((ROOT / "deps.lock.json").read_text())
    assert lock["dependencies"]["luau"]["release"] == "0.733"
    assert len(lock["dependencies"]["luau"]["commit"]) == 40
    assert lock["dependencies"]["godot-cpp"]["godotApiVersion"] == "4.7"
    assert len(lock["dependencies"]["godot-cpp"]["commit"]) == 40


def test_runtime_has_bounded_data_only_surface() -> None:
    source = (ROOT / "src/luau_package_runtime.cpp").read_text()
    bridge = (ROOT / "src/variant_bridge.cpp").read_text()
    for removed in ("os", "debug", "loadfile", "dofile", "collectgarbage", "randomseed"):
        assert removed in source
    assert "require_module" in source
    assert "maximum_memory_bytes" in (ROOT / "src/luau_package_runtime.h").read_text()
    assert "unsupported Godot Variant type" in bridge
    assert "cyclic Luau tables are forbidden" in bridge


def test_extension_declares_only_supported_desktop_targets() -> None:
    descriptor = (ROOT / "addons/luau_gdextension/luau.gdextension").read_text()
    assert "linux." in descriptor and "windows." in descriptor and "macos." in descriptor
    assert "web." not in descriptor and "android." not in descriptor and "ios." not in descriptor


def test_no_native_binary_is_claimed_in_source_bundle() -> None:
    binary_suffixes = {".so", ".dll", ".dylib", ".a", ".lib"}
    tracked = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    ).stdout.split(b"\0")
    binaries = [
        path
        for raw_path in tracked
        if raw_path and (path := Path(raw_path.decode())).suffix.lower() in binary_suffixes
    ]
    assert binaries == []


def test_hardened_build_flags_match_dependency_contracts() -> None:
    sanitizer_workflow = (
        ROOT / ".github/workflows/hardening-sanitizers.yml"
    ).read_text()
    assert "-DASAN_ENABLED -DUBSAN_ENABLED" in sanitizer_workflow

    presets = json.loads((ROOT / "CMakePresets.json").read_text())
    windows = next(
        preset
        for preset in presets["configurePresets"]
        if preset["name"] == "windows-x86_64"
    )
    assert "/wd4714" in windows["cacheVariables"]["WARNING_FLAGS"].split(";")
