# PACT-60 local GREEN evidence

- MQB/MSVC: 41/41 tests passed.
- Repetition: full IK/batch suite passed 25/25 runs.
- CMake/MSVC: 10 incremental build steps; CTest 1/1 passed in 0.10 seconds.
- Source drift: 14 production sources match.

IK evidence:

- Reachable target reached within `1e-4`; both bone lengths unchanged.
- Fully extended, too close, zero-weight, pole-parallel fallback, bounded joint
  limit, and degenerate bone cases remain finite and return typed status.
- Only root/mid local rotations are written; end and sibling locals stay unchanged.
- Compiled TwoBoneIK instruction calls the shared solver and records target/error.

Batch evidence:

- Serial reference and worker counts 1, 2, and 4 produce identical poses/events.
- Results are stably sorted by CharacterId despite deliberately shuffled input.
- Each GraphInstance is required non-null and unique before workers launch.
- Pre-requested cancellation returns `ErrorCode::cancelled` without work.
- Workers use `std::jthread` and are explicitly joined before result return; the
  initial destructor-stop race was observed and fixed before this GREEN evidence.
