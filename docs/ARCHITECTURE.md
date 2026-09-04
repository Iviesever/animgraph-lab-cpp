# Architecture

AnimGraphLab separates immutable data from mutable execution. Raw Skeleton and Clip
values are validated and encoded as versioned assets. `GraphBuilder` produces a
declarative graph; `compile_graph` validates it, eliminates unreachable nodes, emits
a stable instruction order, assigns pose slots/state offsets, folds constants, and
derives canonical JSON plus an FNV-1a identity.

`CompiledSkeleton`, assets, and `CompiledGraph` are shareable value data.
`GraphInstance` owns every character-specific clock, pose slot, cache, transition,
root-motion slot, and IK observation. `evaluate` follows only compiled instructions.
CLI, benchmark, Trace, and Viewer never implement a second animation evaluator.

```text
Raw Skeleton + Clips -> Asset Compiler -> Versioned Assets
                                      -> Declarative Graph
                                      -> Stable Compiler
                                      -> Schedule / Pose Slots / State Layout
                                      -> Per-character Evaluation
                                      -> Pose / Events / Root Motion / Trace
```

Limits are explicit: 256 joints, 4,096 nodes, 16,384 keys per track, 4,096
events/markers, and 64 MiB per asset. Expected failures use typed `Error` values.
MSVC/GCC use `std::expected`; Clang 18's default library uses a narrow compatibility
value with the same value/error surface.
