# Testing

The dependency-free test runner reports 67 named unit/integration tests. Behavior
entered through witnessed missing-interface or regression RED states before GREEN.
Core coverage includes math, Skeleton/Pose, time/sampling, events/markers, three
asset formats, compression, compiler validation, slot reuse, all P0 node executions,
state/root/cache, IK, batch, CLI, Trace, Viewer generation, and benchmark shape.

`animgraph_property` runs 10,000 varied valid and 10,000 typed invalid deterministic
cases plus a 1,000-character serial/parallel oracle. Each valid case executes
reuse/no-reuse plans and checks asset/plan identity. `animgraph_fuzz` runs 100,000 bounded inputs
over Skeleton/Clip/Graph decoders, including random bytes, truncation, bad lengths,
counts/offsets, NaN, checksum corruption, and recomputed-CRC deep paths.

Primary commands:

```powershell
./scripts/run_mqb.ps1 -Target Lab -Configuration Release -ProgramArguments verify
./scripts/run_mqb_tests.ps1 -Configuration Debug
./scripts/run_mqb_verifier.ps1 -Target Property -Configuration Release
./scripts/run_mqb_verifier.ps1 -Target Fuzz -Configuration Release
./scripts/run_cmake_msvc.ps1 -Configuration Debug
./scripts/run_cmake_msvc.ps1 -Configuration Release
```

The MQB wrappers inject the current full Git SHA and the bound-revision test gate.
An ordinary source archive without `.git` remains buildable with an explicit
`unbound-source` provenance; the local delivery Source ZIP carries generated CMake
revision metadata and is rebuilt/tested during package verification.

CI runs Windows MSVC, Ubuntu Clang, Ubuntu GCC, and Clang ASan+UBSan with warnings as
errors. `scripts/check_source_drift.ps1` rejects divergent MQB/CMake library sources.
