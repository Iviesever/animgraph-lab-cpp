# PACT-40 local GREEN evidence

- MQB/MSVC: 27/27 tests passed.
- MQB repeat: 16 compile hits, 0 misses; 1 link hit; 153.231 ms total.
- CMake/MSVC: 7 incremental steps; CTest 1/1 passed in 0.09 seconds.
- Source drift: 9 production sources match.

Verified compiler behavior:

- Invalid ids/pins/types, duplicate target pins, missing inputs, unbound players,
  cycles, duplicate parameters, and non-finite defaults fail closed.
- Full graph is validated before Output-reachability DCE.
- Stable NodeId topology, parameter name sorting, 8-byte state layout, constant
  parameter folding, and deterministic canonical JSON/FNV-1a identity.
- Last-use pose slot reuse reduces a seven-instruction linear plan below the
  seven-slot no-reuse oracle with identical evaluated output.

Verified runtime behavior:

- Runtime consumes only compiler instructions for ReferencePose, ClipPlayer,
  PoseCache, and Output at this PACT boundary.
- Debug checks reject invalid/uninitialized slots and mismatched instance/context.
- Cache hits on same frame/generation/parameter hash, invalidates on parameter or
  generation changes, and remains isolated across character instances.
