# Architecture

## Data flow

```text
Raw Skeleton / Raw Clips
          -> Offline validation and compilation
          -> Versioned .agskel / .agclip assets
          -> Declarative graph description
          -> Stable graph compiler
          -> Immutable schedule + pose slots + state layout
          -> Per-character graph instance evaluation
          -> Local pose -> model pose -> skin palette
          -> Events + markers + root motion + trace
          -> Benchmark report + self-contained HTML viewer
```

## Module boundaries

- `math`: finite Vec3, Quat, Transform operations and interpolation.
- `skeleton`: raw validation, stable parent-before-child compilation, pose spaces.
- `clip`: integer animation time, sampling, playback normalization, events/markers.
- `asset`: explicit little-endian field codecs with CRC32 and bounded decoding.
- `compression`: deterministic constant detection and error-bounded key reduction.
- `graph`: typed builder data, validation, stable topological compilation, plan JSON.
- `runtime`: immutable plan execution with per-instance time/state/cache/root motion.
- `ik`: stable two-bone solve in documented local/model-space boundaries.
- `batch`: serial oracle and bounded `std::jthread` worker evaluation.
- `trace`: one runtime truth model serialized for CLI, reports, and viewer.
- `viewer`: embedded CSS/JavaScript consuming the trace; no second evaluator.

## Storage and limits

IDs are explicit 32-bit strong wrappers. Counts and byte lengths use checked
conversion before allocation. Default hard limits are 256 joints, 4096 graph
nodes, 16384 keys per track, 4096 events/markers per clip, and 64 MiB per asset.
Binary fields are written individually in little-endian order; C++ object layouts
are never persisted.

## Graph compilation

The compiler validates typed edges and required inputs, rejects duplicates/cycles,
eliminates dead nodes from Output reachability, performs stable ID-based topological
ordering, folds constant parameters where valid, computes last uses, and assigns
scratch pose slots only across non-overlapping lifetimes. A slot-reuse-disabled
oracle plan verifies equivalent output. Canonical JSON excludes timestamps, paths,
machine data, and addresses; its stable FNV-1a identity is diagnostic, not security.

## Runtime ownership

`CompiledGraph` and assets are shareable immutable values. `GraphInstance` owns clip
time, state-machine state, transitions, cache generations, event cursors, root-motion
accumulation, and scratch poses. One evaluator consumes the compiled schedule; CLI,
viewer generation, and benchmarks never implement animation semantics independently.

## Errors

Expected validation and parse failures use typed errors returned through
`std::expected`. Public entry points reject non-finite math and out-of-contract
indices. Debug builds additionally guard uninitialized pose slots and invalid state
offsets. Runtime evaluation returns a failure record rather than partial success.

## Testing

Behavior enters through witnessed RED tests. Unit and oracle tests cover math and
each runtime subsystem; deterministic property generators cover 10,000 valid and
10,000 invalid cases; bounded parser fuzz covers 100,000 inputs; MSVC Release and
Clang/GCC sanitizer builds provide complementary evidence. Browser QA checks the
generated viewer against trace values and interactions.
