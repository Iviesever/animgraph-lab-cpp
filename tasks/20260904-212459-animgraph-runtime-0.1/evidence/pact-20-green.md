# PACT-20 local GREEN evidence

- MQB/MSVC tests: exit 0, 18/18 tests passed.
- Parser robustness: 10,000 deterministic random byte inputs were presented to
  skeleton decode, clip decode, and generic inspect without crash or unbounded work.
- CMake/MSVC: incremental build completed; CTest 1/1 passed in 0.07 seconds.
- Source drift: 6 production sources match between MQB and CMake.
- MQB `animc`: 7 production translation units built; compile/inspect/validate all
  exited 0 against real files.

Generated canonical samples:

- `samples/assets/sample.agskel`: 264 bytes, SHA-256
  `039BC266586B3A22FB658789A057A42A9F356CE585F413C91C5578A367F19121`.
- `samples/assets/sample.agclip`: 251 bytes, SHA-256
  `7F7C03647B555B3F342BB448D92E942C31DD893E9633DAE988E81C304D517FD4`.

Covered: zero/negative/exact/large time normalization, loop seam, missing/single
tracks, interpolation, stable event/marker order, byte-stable round trips, magic,
version, length, truncation, CRC corruption, non-finite keys, and file tool flow.
