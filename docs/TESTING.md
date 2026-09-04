# Testing

The dependency-free test runner reports 46 named unit/integration tests. Behavior
entered through witnessed missing-interface or regression RED states before GREEN.
Core coverage includes math, Skeleton/Pose, time/sampling, events/markers, three
asset formats, compression, compiler validation, slot reuse, all P0 node executions,
state/root/cache, IK, batch, CLI, Trace, Viewer generation, and benchmark shape.

`animgraph_property` runs 10,000 valid and 10,000 invalid deterministic cases plus a
1,000-character serial/parallel oracle. `animgraph_fuzz` runs 100,000 bounded inputs
over Skeleton/Clip/Graph decoders, including random bytes, truncation, bad lengths,
counts/offsets, NaN, checksum corruption, and recomputed-CRC deep paths.

Primary commands:

```powershell
./scripts/run_mqb_tests.ps1 -Configuration Debug
./scripts/run_mqb_verifier.ps1 -Target Property -Configuration Release
./scripts/run_mqb_verifier.ps1 -Target Fuzz -Configuration Release
./scripts/run_cmake_msvc.ps1 -Configuration Debug
./scripts/run_cmake_msvc.ps1 -Configuration Release
```

CI runs Windows MSVC, Ubuntu Clang, Ubuntu GCC, and Clang ASan+UBSan with warnings as
errors. `scripts/check_source_drift.ps1` rejects divergent MQB/CMake library sources.
