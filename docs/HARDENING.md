# Luau hardening profile

## Purpose

The upstream extension exposes a broad, Lua-C-API-shaped surface. That is useful
for trusted engine integration, but it should not be treated as a security
boundary by itself. This overlay adds two opinionated profiles:

- **Trusted batched runtime:** one persistent VM, optimization level 2, and one
  coarse bridge call per gameplay subsystem update.
- **Restricted untrusted runner:** a fresh VM for every execution, no Godot/OS/
  debug libraries, immutable globals, bounded source and bytecode, validated
  data-only input, primitive-only output, a VM-wide memory ceiling, and a
  monotonic deadline.

## Native changes

### Compiler allocation ownership

`luau_compile()` returns a compiler-owned allocation. `Luau::compile()` now
copies the bytecode into `PackedByteArray` and calls `std::free()` on the
original allocation.

### Tracked allocator

Main `LuaState` instances are created with `lua_newstate()` and a custom
allocator. Every allocation has a private size header so accounting does not
rely on the allocator's `osize` argument. The following methods are exposed:

- `set_memory_limit_bytes(limit_bytes)`; zero disables the ceiling.
- `get_memory_limit_bytes()`.
- `get_memory_bytes()`.
- `get_peak_memory_bytes()`.
- `did_hit_memory_limit()`.
- `clear_memory_limit_hit()`.

The limit applies to the whole VM, including all Luau coroutines sharing it.
The restricted runner performs bounded host setup first, freezes the environment,
checks that initialized VM usage fits the requested ceiling, and only then enables
the ceiling for untrusted execution. This avoids artificial allocator failures
long-jumping through host-side Variant conversion frames. The limit does not
account for the offline compiler or Godot objects stored outside the VM. Source,
bytecode, and input limits separately bound setup work.

### Monotonic deadlines

`start_timeout_usec(timeout_usec, poll_stride)` installs the Luau interrupt
callback and records a `std::chrono::steady_clock` deadline. The callback also
falls back to the main state's thread data when a Luau-created coroutine does
not yet have a `LuaState` wrapper. `clear_timeout()` removes the deadline, and
`did_timeout()` reports whether it fired.

Optional Godot `interrupt` signals are throttled by
`set_interrupt_signal_stride()`. The default stride is 1024 callbacks. The
restricted runner never enables these signals; it uses interrupts only for the
native deadline.

### Constrained `longjmp` exposure

The pinned Luau build uses `LUAU_EXTERN_C`, which enables long-jump error
handling. The overlay:

- opens libraries behind `lua_pcall`;
- uses raw table writes for host library registration;
- performs raw metatable lookup and protected metamethod invocation;
- runs untrusted code only behind `pcall`;
- exposes no Godot objects or host Callables to the untrusted VM;
- accepts and returns only bounded data.

The low-level `LuaState` API still contains functions whose semantics may invoke
Luau errors or metamethods. Treat it as trusted-only. Do not make it reachable
from user-authored scripts.

## Recommended defaults

`SandboxedLuauRunner` defaults:

| Control | Default |
|---|---:|
| Source | 256 KiB |
| Bytecode | 1 MiB |
| VM memory | 32 MiB |
| Deadline | 50 ms |
| Interrupt poll stride | 64 |
| Return value | 1 MiB |
| Input nesting | 8 |
| Input values | 20,000 |
| Input data | 4 MiB |

Tune these using representative scripts. A frame-sensitive client may need a
lower deadline; an offline tool may allow more. Never disable both time and
memory limits for hostile source.

## Build and test

Linux:

```bash
./hardening/scripts/build_linux_release.sh
```

Apple Silicon macOS:

```bash
./hardening/scripts/build_macos_release.sh
```

Windows PowerShell:

```powershell
./hardening/scripts/build_windows_release.ps1
```

The helper builds Release, runs native tests, synchronizes the addon into the
standalone projects, and runs smoke tests plus the benchmark when a Godot
executable is available.

## Performance rules

- Compile once and retain a VM for trusted gameplay modules.
- Send one compact payload and receive one compact result.
- Avoid per-entity, per-property, or per-vector bridge calls.
- Do not convert large Arrays or Dictionaries every frame.
- Keep scene tree, rendering, physics, networking, and platform APIs in typed
  GDScript or C++.
- Measure the release extension, not the editor's debug library.

The benchmark under `hardening/benchmark` compares an isolated loop through one
coarse call. It intentionally does not claim to represent engine-bound traffic.
