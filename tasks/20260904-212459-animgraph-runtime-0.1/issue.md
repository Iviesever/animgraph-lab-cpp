# Issue: AnimGraphLab 0.1.0

## Human intent

Create and publicly deliver `Iviesever/animgraph-lab-cpp` as a complete,
observable Modern C++23 skeletal-animation runtime and compiled animation-graph
laboratory. The user owns the career direction, project theme, fixed path,
deadline, MQB-first policy, source-only publication policy, and acceptance bar.
The user will not hand-write delivery code in this cycle.

## Success

The repository provides tested Skeleton/Pose math, clip sampling, events and sync
markers, versioned assets, compression, a stable graph compiler, blend/layer/state
runtime, root motion, pose caching, two-bone IK, stable batch evaluation, CLI,
real traces, a self-contained interactive viewer, performance evidence, clean-room
packages, truthful documentation, and a Draft PR.

## Fixed constraints

- C++23 and standard library first.
- Fixed repository path and GitHub owner/name.
- MQB 5.4.0 is the primary Windows developer entry; CMake/CTest remains portable.
- P0 is required; Motion Matching is conditional and never allowed to endanger P0.
- No engine integration, renderer, heavyweight asset importer, release, tag, merge,
  uploaded binary, sibling-repository mutation, or system-level change.
- Feature freeze: 2026-09-05 12:00 JST. Hard stop: 16:00 JST.

## Considered approaches

1. **Compiled instruction runtime (selected).** Builders validate declarative data
   and emit immutable, canonical schedules and layouts. Character instances own
   all mutable state. This best supports deterministic plans, slot reuse, traces,
   batch isolation, serialization, and tests.
2. **Virtual node object hierarchy.** Familiar extensibility, but pointer identity,
   allocation-heavy execution, ownership ambiguity, and difficult serialization
   conflict with the product contract.
3. **Header-only monolith.** Quick initial iteration, but poor compile isolation,
   weak module boundaries, and drift-prone build ownership make it unsuitable for
   a reviewable systems portfolio.

## Decision

Use approach 1 with focused value-data modules and a small instruction evaluator.
The exhaustive user-supplied objective is the approved architecture and acceptance
source; this issue resolves only implementation shape where the objective permits.
