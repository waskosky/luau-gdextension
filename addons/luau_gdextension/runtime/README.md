# Luau runtime wrappers

Production code should use the native `LuauPackageRuntime` class through the
Godot-light `rai.runtime.luau/v1` capability. It maintains one trusted,
data-only Luau virtual machine per active experience scope and accepts a locked
logical module map.

`LuauSandboxRunner` always creates a fresh virtual machine for one call. It has
smaller source, memory, payload, and deadline limits and never shares state with
trusted gameplay sessions.

The older GDScript wrappers remain as migration examples only. They must not be
used as evidence that the native binary is present or production-ready.
