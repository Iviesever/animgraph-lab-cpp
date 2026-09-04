# AnimGraphLab 0.1.0 executable task plan

> **For the agent worker:** use `executing-tasks` for inline execution. Track every
> step with these checkboxes and retain RED/GREEN command output under `evidence/`.

**Goal:** Build, verify, package, push, and open a Draft PR for a complete C++23
skeletal-animation runtime and compiled animation-graph laboratory.

**Architecture:** Immutable versioned assets and compiled tagged-instruction plans
are shared across characters. Each `GraphInstance` owns mutable playback, state,
cache, event, root-motion, and scratch data; one evaluator supplies CLI, batch,
benchmark, trace, and viewer behavior.

**Technology:** ISO C++23, standard library, MQB 5.4.0/MSVC, CMake/CTest, Clang/GCC
and sanitizers where available, PowerShell packaging, embedded HTML/CSS/JavaScript.

## Global constraints

- Root: `D:\program\AnimGraphLab`; GitHub: public `Iviesever/animgraph-lab-cpp`.
- MQB is the primary Windows developer path; CMake/CTest is the portable path.
- Limits: 256 joints, 4096 nodes, 16384 keys/track, 4096 events/markers, 64 MiB/file.
- Runtime pose math uses float with tolerance contracts; no cross-platform bitwise
  pose or rollback-lockstep claim.
- No engine/renderer/importer/ECS scope, sibling-repository writes, merge, tag,
  release publication, remote-branch deletion, or uploaded binary.
- Freeze 2026-09-05 12:00 JST; hard stop 2026-09-05 16:00 JST.

## File structure

- `include/animgraph/core/types.hpp`: stable IDs, errors, limits, animation time.
- `include/animgraph/math/math.hpp`, `src/math/math.cpp`: finite vector/quaternion/transform math.
- `include/animgraph/skeleton/skeleton.hpp`, `src/skeleton/skeleton.cpp`: validation and pose spaces.
- `include/animgraph/clip/clip.hpp`, `src/clip/clip.cpp`: tracks, sampling, events, markers.
- `include/animgraph/asset/codec.hpp`, `src/asset/codec.cpp`: versioned bounded binary codecs.
- `include/animgraph/compression/compression.hpp`, `src/compression/compression.cpp`: reduction metrics.
- `include/animgraph/graph/graph.hpp`, `src/graph/graph.cpp`: builder/compiler/canonical plan.
- `include/animgraph/runtime/runtime.hpp`, `src/runtime/runtime.cpp`: blend/state/cache/root evaluator.
- `include/animgraph/ik/two_bone_ik.hpp`, `src/ik/two_bone_ik.cpp`: stable analytic IK.
- `include/animgraph/runtime/batch.hpp`, `src/runtime/batch.cpp`: serial and joining worker evaluation.
- `include/animgraph/trace/trace.hpp`, `src/trace/trace.cpp`: runtime trace and viewer generation.
- `include/animgraph/samples/procedural.hpp`, `src/samples/procedural.cpp`: programmatic demo assets.
- `src/cli/main.cpp`: all required CLI commands; no duplicate evaluator.
- `tests/test_support.hpp`, `tests/test_main.cpp`, `tests/*_tests.cpp`: dependency-free test executable.
- `tests/property/property.cpp`, `tests/fuzz/fuzz.cpp`: deterministic bounded sweeps.
- `benchmarks/benchmark.cpp`: required benchmark matrix and JSON/Markdown output.
- `scripts/*.ps1`: build drift, evidence, packaging, and clean extraction verification.
- `viewer/template.html`: self-contained trace-driven debugger template.

---

### Task 1: PACT-00 verified baseline

**Files:**

- Create: `include/animgraph/core/version.hpp`
- Create: `src/core/version.cpp`
- Create: `src/cli/main.cpp`
- Create: `tests/test_support.hpp`, `tests/test_main.cpp`, `tests/version_tests.cpp`
- Modify: `CMakeLists.txt`, `mqb.json`, task progress/evidence files

**Interface produced:** `std::string_view animgraph::version_string() noexcept` and
`animgraph_lab --version` printing `AnimGraphLab 0.1.0`.

- [x] Write a version test that includes `animgraph/core/version.hpp`, registers
  `version_string_is_0_1_0`, and asserts equality with `AnimGraphLab 0.1.0`.
- [x] Run the baseline build and capture the expected missing-header RED failure.
- [x] Add the version API and a CLI accepting only `--version` and `verify`; unknown
  commands return 2 with a diagnostic.
- [x] Configure target `animgraph`, `animgraph_lab`, and `animgraph_tests`; enable
  `/W4 /WX /permissive-` or `-Wall -Wextra -Wpedantic -Werror`.
- [x] Run MQB CLI and explicit test source sets twice, then CMake MSVC Debug + CTest.
- [x] Initialize `main`, create the authorized public remote, commit/push the verified
  baseline, fetch, record base SHA, and create `feat/animgraph-runtime-0.1` from it.

### Task 2: PACT-10 math, skeleton, and pose

**Interfaces produced:**

```cpp
struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; }; // xyzw
struct Transform { Vec3 translation; Quat rotation; Vec3 scale; };
std::expected<Quat, Error> normalize(Quat) noexcept;
Quat slerp(Quat a, Quat b, float t) noexcept;
std::expected<CompiledSkeleton, Error> compile_skeleton(const RawSkeleton&);
std::expected<ModelPose, Error> local_to_model(const CompiledSkeleton&, const LocalPose&);
std::expected<SkinMatrixPalette, Error> model_to_skin(const CompiledSkeleton&, const ModelPose&);
```

- [x] Add focused failing tests for normalization, q/-q shortest paths, exact
  interpolation endpoints, 180 degrees, zero/NaN/Inf, compose/inverse, skeleton
  cycles/roots/unsorted remap, pose oracles, and 1/2/64/256-joint bounds.
- [x] Run only the new suite and save the expected missing-interface RED output.
- [x] Implement finite math with guarded normalization and SkeletonBuilder validation,
  stable parent-before-child remapping, local/model transforms, and skin matrices.
- [x] Run the focused suite, all tests, MQB Debug twice, and CMake MSVC Debug.
- [x] Update evidence/progress/handoff, commit `feat: add animation math and poses`, push.

### Task 3: PACT-20 clip sampling, events, markers, and asset version 1

**Interfaces produced:**

```cpp
struct AnimTime { std::int64_t ticks; static constexpr std::int64_t ticks_per_second=48000; };
enum class ClipPlaybackMode { clamp, loop, ping_pong };
std::expected<SampleResult, Error> sample_clip(const CompiledSkeleton&, const AnimationClip&, AnimTime);
std::vector<AnimationEvent> query_events(const AnimationClip&, AnimTime from, AnimTime to);
std::expected<std::vector<std::byte>, Error> encode_skeleton(const CompiledSkeleton&);
std::expected<CompiledSkeleton, Error> decode_skeleton(std::span<const std::byte>);
std::expected<std::vector<std::byte>, Error> encode_clip(const AnimationClip&);
std::expected<AnimationClip, Error> decode_clip(std::span<const std::byte>);
```

- [x] Add failing tests for clamp/loop/ping-pong, zero/end/negative/large time,
  one/missing keys, seam events without duplicates, marker ordering, byte-stable
  round trips, bad magic/version/endian/CRC/offset/count/float/truncation.
- [x] Run the focused suite and save the missing-interface RED output.
- [x] Implement sorted tracks, reference-pose fill, shortest-path rotation sampling,
  half-open event intervals, marker policy, explicit little-endian fields and CRC32.
- [x] Add `animc compile`, `inspect`, and `validate` plumbing through codec APIs.
- [x] Run focused/all/MQB/MSVC tests and an initial 10,000-input malformed corpus.
- [x] Update records, commit `feat: add clips events and versioned assets`, push.

### Task 4: PACT-30 deterministic compression

**Interface produced:**

```cpp
struct CompressionSettings { float translation_error; float rotation_radians_error; float scale_error; };
struct CompressionReport { std::size_t raw_keys, compressed_keys, raw_bytes, compressed_bytes; float max_translation_error, max_rotation_error, max_scale_error; };
std::expected<CompressedClip, Error> compress_clip(const AnimationClip&, CompressionSettings, CompressionReport&);
```

- [x] Add failing raw-versus-compressed grid/random-time oracle tests for static,
  walk, rapid rotation, tiny motion, long, and nonuniform clips.
- [x] Witness RED, then implement deterministic constant detection and linear key
  removal using positional, quaternion-angle, and scale error metrics.
- [x] Verify every sampled error is within settings and output stable report bytes.
- [x] Run all regression/toolchain checks, save report, update records, commit
  `feat: add deterministic clip compression`, push.

### Task 5: PACT-40 compiled graph and evaluator

**Interfaces produced:**

```cpp
enum class NodeType { reference_pose, clip_player, blend_1d, blend_2d, additive, layered_blend_per_bone, pose_cache, two_bone_ik, state_machine, output };
std::expected<CompiledGraph, Error> compile_graph(const GraphDescription&, bool reuse_slots=true);
std::string canonical_plan_json(const CompiledGraph&);
std::uint64_t plan_identity(const CompiledGraph&);
std::expected<EvaluationResult, Error> evaluate(const EvaluationContext&, const CompiledGraph&, GraphInstance&);
```

- [x] Add failing tests for typed pins, duplicates, missing inputs, cycles, stable topo,
  dead nodes, constant values, state layout, last-use slots, reuse-disabled oracle,
  stable JSON/identity, cache hits/invalidation/isolation, and schedule-only execution.
- [x] Witness RED, implement builder validation and stable ID topological compilation.
- [x] Implement liveness slot allocation, canonical JSON, FNV-1a identity, debug slot
  guards, parameter/state layout, and per-instance evaluation.
- [x] Run all checks, repeat plan identity, update records, commit
  `feat: compile and evaluate animation graphs`, push.

### Task 6: PACT-50 blend trees, layers, state machine, and root motion

**Interfaces consumed:** Task 5 node types and evaluator. **Interfaces produced:**
validated `Blend1DSample`, `Blend2DSample`, `LayerMask`, `StateMachineDefinition`,
`TransitionDefinition`, and `RootMotionDelta` value data executed by compiled nodes.

- [x] Add failing tests for sorted/clamped/exact/duplicate Blend1D, triangle weights and
  degenerate Blend2D, local additive rotations, hierarchy masks, transition priority,
  zero duration/interruption/exit time/marker sync, state events, and root seams/crossfades.
- [x] Witness RED and implement only the tested blend/state/root contracts.
- [x] Document crossfade event ownership and root-motion remove/retain policy in code
  contracts and trace fields.
- [ ] Run all checks and stable event-order repetitions, update records, commit
  `feat: add blend state and root motion runtime`, push.

### Task 7: PACT-60 two-bone IK and character batches

**Interfaces produced:**

```cpp
std::expected<IkResult, Error> solve_two_bone_ik(const CompiledSkeleton&, LocalPose&, const TwoBoneIkRequest&);
std::vector<EvaluationResult> evaluate_serial(std::span<EvaluationJob>);
std::vector<EvaluationResult> evaluate_parallel(std::span<EvaluationJob>, std::size_t workers, std::stop_token={});
```

- [ ] Add failing tests for reachable/extended/too-close/degenerate/pole/weight cases,
  finite output, bone lengths, chain isolation, cancellation, join behavior, stable
  CharacterId order, event order, and serial/parallel tolerance equivalence.
- [ ] Witness RED, implement analytic IK with stable fallback and optional angle clamp.
- [ ] Implement fixed bounded `std::jthread` character workers with disjoint instance
  and scratch ownership; never detach.
- [ ] Run all checks repeatedly at worker counts 1/2/4, update records, commit
  `feat: add two bone IK and batch evaluation`, push.

### Task 8: PACT-70 sample, trace, CLI, viewer, and benchmark

**Interfaces produced:** `make_procedural_humanoid()`, `make_locomotion_demo()`,
`TraceDocument`, `write_trace_json`, and `generate_viewer_html` plus all CLI verbs.

- [ ] Add failing integration tests invoking sample/evaluate/benchmark/compile/inspect/
  generate-viewer/verify APIs and checking required trace fields and embedded assets.
- [ ] Witness RED; implement procedural Idle/Walk/Run/Turn/Aim/layer/state/root/IK demo.
- [ ] Serialize real runtime frames with skeleton, poses, graph/state/transition, clip
  times, weights, cache, events, markers, root motion, IK/error, compression, timing,
  versions, Git SHA, and success.
- [ ] Implement an offline viewer with Canvas skeleton, hierarchy, controls, scrub,
  spaces, graph/state/blend/root/IK/event/marker/compression views and responsive CSS.
- [ ] Run required benchmark matrix and store raw JSON plus a machine-qualified report.
- [ ] Open generated HTML in a browser, verify console zero errors and interactions,
  compare trace/root/state values, narrow viewport, and capture the committed screenshot.
- [ ] Update records, commit `feat: add trace viewer and benchmark tooling`, push.

### Task 9: Property, fuzz, sanitizer, documentation, and packages

- [ ] Implement deterministic seeds for 10,000 valid and 10,000 invalid bounded
  skeleton/clip/graph cases; assert typed failure, invariants, identities, compression,
  and serial/parallel behavior.
- [ ] Implement 100,000 bounded codec inputs including recomputed-CRC deep-parser data;
  run MSVC Release and an available ASan/UBSan Clang/GCC environment.
- [ ] Run MQB Debug/Release, CMake MSVC Debug/Release, CTest, available Clang and GCC,
  and source-set drift verification; record missing environments honestly.
- [ ] Write every required document under `docs/`, including 12 concrete live-change
  drills and exact AI authorship; align README claims with executable evidence.
- [ ] Generate clean-HEAD Win64/source ZIPs, manifest, SHA-256 files; extract into
  `artifacts/verification/extracted-win64`, run `verify`, `evaluate`, and
  `generate-viewer`, then remove only that exact verified extraction directory.
- [ ] Commit `test: complete verification and delivery artifacts`, push.

### Task 10: Independent audit and Draft PR

- [ ] Dispatch at most two read-only reviewers with non-overlapping scopes: math/graph/
  compression and tests/viewer/performance/packaging. Reviewers do not edit files.
- [ ] Classify findings; for every confirmed Blocker/High write and witness a RED test,
  apply the minimal fix, and rerun the affected plus full gates. Add only directly
  related low-risk Medium fixes.
- [ ] Rebuild artifacts after fixes so binary/source ZIP, manifests, reports, trace,
  screenshot, and SHA all bind to the same clean HEAD.
- [ ] Verify fetch, base, ahead/behind, commit list, clean status, remote branch, and
  all final completion criteria; push the final checkpoint.
- [ ] Create/update a Draft PR with architecture, tests, property/fuzz totals,
  performance, viewer, artifact paths/SHA, limitations, and AI authorship. Do not
  merge, tag, release, or upload binary assets.
- [ ] Record the final report fields and recommend merge/publication only if supported
  by all evidence.

## Plan self-review

- Specification coverage: PACT tasks cover all required P0 nodes and subsystems;
  Task 9 covers cross-toolchain/property/fuzz/docs/package; Task 10 covers audits/PR.
- Placeholder scan: no deferred implementation or undefined placeholder steps.
- Type consistency: the compiler emits `CompiledGraph`; evaluator consumes it with
  `GraphInstance`; serial/parallel jobs use the same evaluator and result type.
- Execution selection: the user mandated one main agent; inline execution is selected,
  with no more than two final read-only audit agents.
