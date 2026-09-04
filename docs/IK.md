# Two-Bone IK

The solver reads Root/Mid/End model positions, preserves both measured bone lengths,
and solves a law-of-cosines bend plane toward a model-space target. Pole projection
chooses bend direction; a parallel/zero pole uses a stable orthogonal fallback.

Requested distance is clamped between the limb's close and extended limits. Optional
bend limits adjust the solved distance. Only Root and Mid local rotations are
written, blended by weight; End and unrelated joints are untouched. Results report
reached/extended/too-close/degenerate status, achieved position, target error, bend
angle, and pole-fallback use. The compiled TwoBoneIK node calls this same solver.
