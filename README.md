# AnimGraphLab

AnimGraphLab is a from-scratch Modern C++23 laboratory for an engine-independent,
data-oriented skeletal-animation runtime. The repository starts from a verified
construction baseline; capabilities are documented here only after executable
verification exists.

```text
Raw Clips + Skeleton
        ↓
Versioned Asset Compiler
        ↓
Compiled Animation Graph
        ↓
Blend / State / Layer / Root Motion / IK
        ↓
Batch Pose Evaluation
        ↓
Compression Metrics + Interactive Debugger
```

The project is AI-assisted engineering. The user specified the product direction,
scope, constraints, and acceptance bar; Codex GPT-5.6 Sol is responsible for the
implementation and evidence in this delivery cycle.

## Verified baseline

- MQB 5.4.0 discovers MSVC and builds/runs the CLI and dependency-free tests.
- CMake/Ninja builds the same source manifest with MSVC and runs CTest.
- GitHub CI exercises Windows MSVC plus Ubuntu Clang and GCC.

The runtime feature work is tracked under
`tasks/20260904-212459-animgraph-runtime-0.1/`.

## Verified runtime foundations

- Finite `Vec3`, xyzw `Quat`, and TRS `Transform` math with guarded normalization,
  shortest-path interpolation, axis/from-to rotations, composition, and inversion.
- Skeleton validation with one-root/cycle/parent/count checks and stable
  parent-before-child compilation.
- Local-pose to model-pose evaluation and model-to-skin matrix palettes.
- 48 kHz integer animation time with Clamp/Loop/PingPong normalization, reference
  pose completion, Vec3 interpolation, shortest-path quaternion sampling, ordered
  events, and sync markers.
- Byte-stable little-endian `.agskel`/`.agclip` version 1 assets with explicit
  bounds, offsets, string tables, finite-value checks, and CRC32 integrity.
- A byte-stable `.aggraph` version 1 envelope for canonical compiled-plan JSON.
- `animc compile|inspect|validate` for programmatic sample runtime assets.
- Deterministic constant-track detection and error-bounded translation, quaternion,
  and scale key reduction with per-track byte/key/error reports.
- Declarative graph validation and compilation with typed pose connections, full
  cycle checks, dead-node elimination, stable topology, constant parameter folding,
  state layout, last-use pose-slot reuse, canonical plan JSON, and stable identity.
- Per-character graph instances with compiled ReferencePose/ClipPlayer/PoseCache/
  Output execution, debug slot guards, cache invalidation, and instance isolation.
- Blend1D/triangle Blend2D, local-reference additive poses, hierarchy-propagated
  per-joint layers, deterministic state transitions/interruption, sync-marker time
  mapping, and loop-safe root-motion deltas.
- Analytic model-space Two-Bone IK with stable pole fallback, reach/close handling,
  optional bend limits, weighted local-rotation output, and compiled graph execution.
- Stable CharacterId-ordered serial and bounded `std::jthread` batch evaluation with
  unique instance ownership, cancellation, and explicit join-before-return.
- A 15-joint procedural humanoid with Idle/Walk/Run/Turn/Aim/UpperLayer clips and a
  compiled sample graph containing every P0 node type plus Foot and Hand IK passes.
- Real runtime Trace JSON, a self-contained Canvas debugger, an observed benchmark
  matrix, and CLI commands for sample/evaluate/benchmark/assets/viewer/verification.
- Deterministic 10,000-valid/10,000-invalid property verification and a 100,000
  input three-format fuzz harness with recomputed-CRC deep-parser cases.

## Quick start

```powershell
# Primary Windows/MSVC developer path
mqb run --profile release -- verify

# Portable project path (the script enters the discovered VS developer environment)
./scripts/run_cmake_msvc.ps1 -Configuration Release

# Produce an inspectable run
./.mqb/bin/animgraph_lab.exe evaluate --sample locomotion --trace trace.json --git-sha local
./.mqb/bin/animgraph_lab.exe generate-viewer --trace trace.json --out viewer.html
```

The CLI commands are `sample`, `evaluate`, `benchmark`, `compile-asset`,
`inspect-asset`, `generate-viewer`, and `verify`. The separate `animc` tool exposes
`compile`, `inspect`, and `validate` for versioned runtime assets.

## Verification

- Unit/integration: 58 named tests.
- Property: 10,000 valid + 10,000 invalid bounded cases and 1,000 serial/parallel jobs.
- Fuzz: 100,000 bounded inputs across Skeleton, Clip, and Graph formats.
- Toolchains: MSVC Debug/Release, Ubuntu Clang/GCC, and Clang ASan+UBSan.
- Evidence and exact commands: `tasks/20260904-212459-animgraph-runtime-0.1/evidence/`.

The sample debugger is `viewer/animgraph_debugger.html` and consumes the committed
real Trace at `samples/trace/locomotion.trace.json`. Automated browser loading of a
local file was blocked by the harness URL policy, so interactive browser QA and a
browser screenshot are not claimed; static control/embedding/responsive checks pass.

## Determinism boundary

Identical assets, parameters, and binary produce stable behavior. Asset bytes,
canonical Graph Plans, identities, and event order are stable. Pose floats are
compared with declared tolerances across compilers; this is not a cross-platform
bitwise-deterministic rollback simulation.

## Publication policy

The candidate is source-only on GitHub. Local Win64 packages are verified locally
and are not uploaded as release assets. No tag or release is created in this cycle.

## License

MIT. See `LICENSE`.
