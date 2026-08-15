# Security model

The host assumes that first-party gameplay source has already passed RAI package
validation and signature policy, but it still treats every runtime payload as
bounded input.

Enforced boundaries include:

- exact logical module map; no filesystem-based `require`;
- no operating-system or debug libraries;
- no `require`, `loadfile`, `dofile`, or `collectgarbage` globals;
- no `math.random` or `math.randomseed`;
- sandboxed read-only standard globals;
- combined source, module-count, payload, depth, item, memory, and deadline
  limits;
- finite numbers and exact portable integer range;
- string-only object keys, consecutive positive array keys, and no cyclic table
  results;
- no Godot Object, Node, Resource, Signal, Callable, RID, or arbitrary Variant
  bridge support.

A memory-limit or deadline failure marks the runtime fatal. It rejects further
calls until the Godot scope closes it. Untrusted execution always uses a fresh
virtual machine.

Dependency revision locks establish reproducibility but do not replace source
review, software-composition analysis, compiler hardening, artifact signing, or
platform-specific release testing.
