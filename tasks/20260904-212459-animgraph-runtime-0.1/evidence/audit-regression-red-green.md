# Independent audit regression cycle 1

Two independent read-only reviewers evaluated `e1b85fb..0f7c659` and returned a
not-ready verdict. This checkpoint addresses confirmed safety/correctness findings
that do not require the compiled-node configuration redesign.

Witnessed RED cases:

- non-finite approximate assertion silently passed;
- non-uniform rotated TRS compose/inverse returned an inexact Transform;
- Blend2D `q/-q` edge produced a zero quaternion;
- exit phase crossed by a large delta did not transition;
- corrupted GraphInstance inner vectors/output slot were not validated;
- finite-component IK target overflowed derived norms;
- CRC-valid `{garbage}` Graph payload was accepted;
- rotation key reduction exceeded error between original keys;
- inactive branch events leaked and three loop occurrences collapsed to one;
- same-frame PoseCache hit advanced upstream clip time.

GREEN evidence:

- MQB/MSVC: 56/56 named tests passed.
- CMake/MSVC Debug: 34 build steps; unit/integration, Property, and Fuzz 3/3 passed.
- Source drift: 17 production sources match.

Changes include finite assertion operands, fail-closed shear boundaries, complete
instance dimension checks, safe IK derived values, strict JSON syntax/node-count
validation, dense quaternion path validation, event occurrence identity/branch
propagation, evaluation memo before side effects, runtime root accumulation, and
runtime-produced observations copied by Trace.
