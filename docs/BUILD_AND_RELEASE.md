# Build and release runbook

## Dependency verification

```bash
python3 scripts/bootstrap_dependencies.py
python3 scripts/verify_dependencies.py
```

Both checkouts must be clean and exactly match `deps.lock.json`.

## Desktop builds

```bash
python3 scripts/build.py --target template_debug --build-type RelWithDebInfo
python3 scripts/build.py --target template_release --build-type Release
```

On macOS, `--arch x86_64` and `--arch arm64` select both the compiler target
and the architecture-qualified library name. Never install an unqualified
macOS library: Godot must select the matching host architecture from the
extension descriptor.

Build every declared operating system and architecture on a matching host or
approved cross-compilation runner. Do not rename a library without changing
`addons/luau_gdextension/luau.gdextension` and the release inventory together.

## Godot integration

Copy the whole add-on directory into Godot-light:

```bash
python3 scripts/install_into_godot_light.py \
  --godot-project ../godot-light-main
```

Run with the exact Godot 4.7 editor/export template used for release:

```bash
godot --headless --path ../godot-light-main \
  --script res://modules/openverse.runtime.luau/tests/native_runtime_smoke.gd
```

Then run the experience-package smoke test and portable conformance vectors.

Release validation also opens and closes a real Godot editor project. This
guards the Godot 4.7 static `ClassDB` shutdown path exercised when the resource
saver receives an engine resource type such as `PackedScene`; standalone game
smokes alone do not cover that lifecycle.

Hosts that use only `LuauPackageRuntime` for data-only portable modules may
build with `scripts/build.py --without-script-resource-formats`. Native Luau
classes remain available, while `.lua` and `.luau` files are not registered as
Godot editor resources and therefore do not acquire editor-generated UID
sidecars. The build default remains enabled for projects that use `LuauScript`
assets.
A successful C++ compilation without those engine tests is not a release gate.

## Release receipt

Record at least the dependency lock digest, source digest, compiler identity,
Godot API version, build target, architecture, produced filename, byte length,
SHA-256 digest, and smoke-test result. Sign the binary inventory through the
normal release system.
