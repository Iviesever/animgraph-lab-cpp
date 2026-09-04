# Root motion

Root motion is extracted from an explicit JointId over an animation-time interval.
Looping intervals are split at each seam and their delta Transforms are composed, so
8→12 on a 0→10 translation loop yields +4 rather than -6. Intervals are bounded to
4,096 segments and invalid/reversed inputs fail closed.

The evaluator returns a delta Transform; it never changes game-world state. Pose
policy is explicit: keep the animated root or replace it with identity. Blend1D,
Blend2D, Layer, and State nodes blend contributing deltas. Additive nodes retain the
base root-motion delta, preventing aim offsets from moving the character.
