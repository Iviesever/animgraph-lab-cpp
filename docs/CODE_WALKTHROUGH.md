# Code walkthrough

1. Start at `samples/procedural.cpp`: it builds the 15-joint Skeleton, six Clips,
   and a Graph containing all ten P0 node types.
2. Follow `graph/graph.cpp`: validation, topology, DCE, parameter/state layouts,
   pose liveness, canonical JSON, identity.
3. Read `runtime/runtime.cpp`: the instruction loop, slot guards, clip clocks,
   cache keys, node dispatch, root-motion slots, event de-duplication.
4. Visit `clip/clip.cpp` and `asset/codec.cpp`: integer time and bounded formats.
5. Inspect `runtime/blend.cpp`, `state_machine.cpp`, `root_motion.cpp`, and
   `ik/two_bone_ik.cpp` for pose semantics.
6. See `runtime/batch.cpp` for character-level parallelism and explicit joins.
7. End at `trace/trace.cpp` and `app/lab.cpp`: observation, benchmark, Viewer, CLI.

Tests mirror these boundaries. The active task folder contains every RED/GREEN and
toolchain checkpoint needed to reconstruct the delivery sequence.
