# Skeleton and pose

Raw joints contain name, optional raw parent, reference-local Transform,
inverse-bind Transform, and optional semantic. Validation requires one root,
non-empty names, valid parents, no self-parent/cycle, finite invertible transforms,
and at most 256 joints.

Compilation emits a stable parent-before-child order plus both remap directions.
`LocalPose` is indexed in compiled order. `local_to_model` composes through the
parent chain; `model_to_skin` composes each model transform with its inverse bind and
emits a column-major 4x4 palette. Independent tests cover unordered input, cycles,
1/2/64/256 limits, size mismatches, and local/model/skin reference values.
