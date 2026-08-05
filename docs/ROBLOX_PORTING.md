# Roblox Luau to Godot porting boundary

## Code that usually ports cleanly

Keep platform-neutral gameplay modules in Luau:

- combat and ability calculations;
- inventories and crafting rules;
- quest and dialogue state;
- artificial-intelligence decisions;
- state machines;
- procedural generation;
- validation and deterministic simulation code.

Put engine-facing work in typed GDScript or C++:

- Nodes and scene ownership;
- `_ready`, `_process`, and physics callbacks;
- input, animation, rendering, and audio;
- networking and replication;
- persistence and platform services;
- signals and object lifetime.

## Compatibility adapter

Do not attempt a complete Roblox runtime clone. Define a small project-owned API
and adapt old modules to it. Typical neutral interfaces are:

- `world.query(...)`;
- `entities.get_state(ids)`;
- `commands.emit(batch)`;
- `time.step_seconds`;
- `random.next_*()` with a project-controlled seed;
- `storage.read/write` only in trusted host code.

For performance, pass one state snapshot into Luau and return one command batch.
Do not emulate Roblox by forwarding every property access to a Godot Node.

## Roblox-specific replacements

These require deliberate adapters or rewrites:

- `game:GetService`, `workspace`, and `Instance`;
- `ModuleScript` and Roblox `require` semantics;
- `task.spawn`, `task.wait`, and Roblox scheduling;
- `RBXScriptSignal` and connections;
- `CFrame`, `UDim2`, `Color3`, and `Enum`;
- RemoteEvents, RemoteFunctions, replication, and ownership;
- DataStore, Marketplace, Players, UserInputService, and asset IDs.

Do not connect a standalone client to private Roblox protocols, reuse Roblox
backend credentials, or carry Creator Store assets off platform without a
separate license.

## Trust separation

First-party ported code may use `BatchedLuauRuntime`. Player-authored code must
use `SandboxedLuauRunner` or an equivalently restricted native host. Never add
`LIB_GODOT`, `LIB_OS`, or `LIB_DEBUG` to the untrusted profile, and never pass
Godot Objects or Callables into it.
