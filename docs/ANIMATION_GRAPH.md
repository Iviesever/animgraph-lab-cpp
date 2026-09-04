# Animation graph

P0 node types are ReferencePose, ClipPlayer, Blend1D, Blend2D, Additive,
LayeredBlendPerBone, PoseCache, TwoBoneIK, StateMachine, and Output.

The compiler rejects unknown ids, invalid pins/types, duplicate target inputs,
missing required poses, unbound clip players, cycles, duplicate parameters, and
non-finite defaults. It validates the full graph before Output reachability removes
dead nodes. A NodeId-ordered Kahn pass provides stable topology.

Last-use analysis assigns the smallest free PoseSlot only when its prior lifetime
ends before the current instruction. A reuse-disabled plan is the test oracle.
Instance state offsets are 8-byte aligned. Constant parameters leave the runtime
layout and enter a canonical constant table. Canonical JSON includes only semantic
plan data—never time, machine, path, or pointer identity—and drives the stable ID.
