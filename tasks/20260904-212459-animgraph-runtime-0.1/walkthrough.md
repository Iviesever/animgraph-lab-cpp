# AnimGraphLab 0.1.0 walkthrough

## Verification log

The final implementation tree was verified before this documentation-only
walkthrough commit.

```text
scripts/run_mqb_tests.ps1 -Configuration Release
67/67 tests passed
exit 0

scripts/run_mqb_verifier.ps1 -Target Property -Configuration Release
PROPERTY valid=10000 invalid=10000 batch_cases=1000 batch_match=true
exit 0

scripts/run_mqb_verifier.ps1 -Target Fuzz -Configuration Release
FUZZ inputs=100000 max_bytes=1024 deep_crc=true formats=3
verified_valid=8333 verified_invalid=83333 random=8334
exit 0

scripts/run_cmake_msvc.ps1 -Configuration Debug
100% tests passed, 0 tests failed out of 3
Total Test time (real) = 64.27 sec
exit 0

scripts/run_cmake_msvc.ps1 -Configuration Release
100% tests passed, 0 tests failed out of 3
Total Test time (real) = 4.58 sec
exit 0

scripts/verify_release.ps1 -Package <Win64> -SourcePackage <Source>
PACKAGE_VERIFY success trace=270647 viewer=368689
Source extraction: 38/38 build steps
Source CTest: 100% tests passed, 0 tests failed out of 3
SOURCE_PACKAGE_VERIFY success
exit 0

GitHub Actions run 33899182434
windows-msvc: success
ubuntu (ninja-clang-debug): success
ubuntu (ninja-gcc-debug): success
ubuntu-sanitizers (ASan+UBSan): success
```

The package verifier checks both SHA-256 sidecars, every Win64 manifest entry,
both executable build revisions, Trace and Benchmark revisions, exact Base64 Trace
embedding in the Viewer, generated Source-file hashes, and a no-own-`.git` Source
rebuild. Delivery archives are regenerated after the final documentation commit,
so their exact SHA-256 values live in `artifacts/release/DELIVERY_MANIFEST.json` and
the Draft PR rather than in a self-referential tracked file.

## Change audit

Core implementation areas:

- `include/animgraph/math/` and `src/math/`: finite Vec3/Quat/Transform operations.
- `include/animgraph/skeleton/` and `src/skeleton/`: validation, stable compilation,
  local/model/skin pose conversion.
- `include/animgraph/clip/` and `src/clip/`: 48 kHz time, sampling, events, markers.
- `include/animgraph/asset/` and `src/asset/`: bounded version-1 codecs and tools.
- `include/animgraph/compression/` and `src/compression/`: deterministic reduction
  with bounded exhaustive rotation proof.
- `include/animgraph/graph/` and `src/graph/`: typed builder, stable compiler,
  liveness slots, canonical plan and identity.
- `include/animgraph/runtime/` and `src/runtime/`: per-character evaluation, blends,
  state/marker synchronization, root motion, cache, and stable batches.
- `include/animgraph/ik/` and `src/ik/`: guarded model-space Two-Bone IK.
- `include/animgraph/trace/` and `src/trace/`: runtime observations, benchmark, and
  self-contained HTML debugger.
- `tests/`: 67 unit/integration tests plus deterministic Property and Fuzz programs.
- `scripts/`: SHA-bound MQB entry, CMake build, source drift, dual-package creation,
  and clean-extraction verification.
- `docs/` and `README.md`: architecture, contracts, interview guide, drills,
  limitations, AI disclosure, and source-only policy.

## Acceptance walkthrough

1. `animc` compiles, inspects, and validates bounded `.agskel`, `.agclip`, and
   `.aggraph` version-1 assets; corrupted version, layout, float, CRC, and canonical
   graph schema inputs fail closed.
2. The graph compiler emits one immutable typed schedule with deterministic topology,
   dead-node elimination, constant folding, aligned state, and lifetime-safe pose
   slots. Runtime does not derive a second schedule.
3. The procedural 15-joint demo evaluates all ten P0 nodes, state transitions,
   active-branch event/marker occurrences, root accumulation, and Foot/Hand IK.
4. Serial and four-worker `std::jthread` batches retain input CharacterId order and
   match the serial pose/event oracle.
5. Release packaging regenerates a 60-frame real Trace, nine-scenario benchmark,
   and exact embedded Viewer from the same final-SHA binary for both local archives.
6. Two independent read-only audit tracks ended with zero remaining Blocker/High.

## Honest boundary

The permitted browser surface rejected local `file://` content and explicitly
prohibited localhost or alternate-browser workarounds. Static Viewer tests pass,
but interactive controls, console-zero, narrow viewport, and screenshot remain
unverified. The PR therefore remains Draft; merge and source-only publication are
not recommended until a human completes that browser check.
