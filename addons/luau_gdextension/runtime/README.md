# Runtime wrappers

- `BatchedLuauRuntime` is for trusted, first-party gameplay modules. It keeps one
  VM alive, compiles once, and makes coarse calls with compact payloads.
- `SandboxedLuauRunner` is for untrusted source. It creates a fresh VM per run,
  omits OS/debug/Godot libraries, freezes the environment, limits memory and
  time, validates input, and permits only conservative return types.

Do not pass Nodes, Resources, Callables, RIDs, Signals, or arbitrary Godot
Variants into `SandboxedLuauRunner`. Do not add `LIB_GODOT`, `LIB_OS`, or
`LIB_DEBUG` to its library mask.
