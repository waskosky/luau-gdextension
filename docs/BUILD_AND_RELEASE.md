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
A successful C++ compilation without those engine tests is not a release gate.

## Release receipt

Record at least the dependency lock digest, source digest, compiler identity,
Godot API version, build target, architecture, produced filename, byte length,
SHA-256 digest, and smoke-test result. Sign the binary inventory through the
normal release system.
