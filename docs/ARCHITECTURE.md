# Native host architecture

`LuauPackageRuntime` is deliberately narrower than a Godot language binding.
It does not expose Godot classes or reflection. Godot verifies a compiled
experience bundle and supplies a dictionary of logical module identifiers to
exact Luau source strings. The host compiles those sources inside the process,
caches each module once per virtual machine, and exposes only `require_module`.

The entrypoint contract is:

- `activate(context) -> session`
- `handle_event(session, event) -> result`
- `snapshot(session) -> snapshot`
- `deactivate(session) -> value`

The mutable session is pinned inside the virtual-machine registry and is never
round-tripped through Godot between calls. Activation may return one bounded
data-only projection for startup commands; normalized events, outputs, and
snapshots are the other values that cross the bridge. This reduces allocation
pressure and prevents raw engine objects from becoming portable state.

One trusted runtime instance belongs to one active experience scope. Module
imports are cached in that instance. A module import cycle, unknown logical
identifier, nil module result, source-limit breach, bridge-limit breach, memory
limit, or execution deadline fails closed.

`LuauSandboxRunner` constructs a new `LuauPackageRuntime` for each invocation,
uses smaller limits, and destroys it before returning. It is a separate trust
path and must not receive trusted-session state or capabilities.
