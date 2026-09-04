# PACT-10 local GREEN evidence

- MQB/MSVC command: `scripts/run_mqb_tests.ps1 -Configuration Debug`
- First GREEN: exit 0, 9/9 tests passed, 6 compile misses and 1 link miss.
- Repeat GREEN: exit 0, 9/9 tests passed, 6 compile hits and 1 link hit,
  total 23.341 ms.
- CMake/MSVC command: `scripts/run_cmake_msvc.ps1 -Configuration Debug`
- CMake result: exit 0, 8 incremental build steps; CTest 1/1 passed.
- Drift command: `scripts/check_source_drift.ps1`
- Drift result: exit 0, 3 production sources match.

Covered contracts: invalid/zero/non-finite normalization, xyzw quaternion rotation,
q/-q shortest-path interpolation, exact endpoints, 180-degree rotation,
multiply/inverse/from-to rotation, TRS compose/inverse, invalid root/cycle/bind,
stable unsorted remapping, local/model/skin oracle, and 1/2/64/256/257 bounds.
