# Live change drills

Complete at least one without AI before presenting the project.

1. Change animation tick rate and update the exact-end tests.
2. Switch Clip rotation sampling from SLerp to NLerp and compare angular error.
3. Add a new sync marker to Walk and prove seam ordering.
4. Tighten translation compression tolerance and regenerate the report.
5. Add a Skeleton semantic lookup without exposing internal pointers.
6. Add a Blend1D sample and test exact threshold selection.
7. Add a hierarchy-mask override on a deeper joint and test propagation.
8. Add a non-interruptible transition and demonstrate ownership across frames.
9. Change Root Motion pose policy for one CLI sample and update Trace assertions.
10. Add an IK pole fallback regression for a different principal axis.
11. Add a batch duplicate-CharacterId policy and serial/parallel tests.
12. Add a new compiled Pose pass-through node: schema, cycle/type checks, instruction,
    runtime execution, canonical-plan identity, unit test, Trace label, and docs.

For every drill: write the failing test, record RED, implement minimally, run MQB and
CMake, inspect the diff, and explain rollback.
