# Animation math

`Vec3`, `Quat`, and `Transform` are small value types. Quaternion storage is
explicitly **x, y, z, w**. Public normalization rejects zero length and non-finite
components. Multiplication is Hamilton product; vector rotation uses a normalized
quaternion. `q` and `-q` are treated as the same rotation by shortest-path NLerp,
SLerp, and angular error.

Interpolation returns the exact first or second operand at `t=0` and `t=1`.
Axis-angle and from-to rotation reject invalid axes and use a stable orthogonal axis
for the 180-degree case. TRS composition applies child scale, then parent rotation,
then parent translation. Transform inversion rejects zero scale.

Non-uniform TRS cannot represent every inverse/shear composition exactly; the runtime
does not claim a general affine decomposition. Animation data is expected to use
ordinary skeletal scales, and tests use numeric tolerances rather than byte equality.
