# Compression

Compression first detects tracks whose values remain inside the declared error of
the first key. Non-constant tracks use deterministic maximum-error splitting: keep
endpoints, find the first largest deviation from endpoint interpolation, split when
the threshold is exceeded, and repeat. There is no hash-order dependence.

Translation/scale errors are Euclidean distances; rotation error is quaternion
angular distance. Reports contain raw/compressed keys and estimated bytes plus the
three maxima per joint and globally. Raw and reduced clips are compared on uniform
and deterministic random times for static, walk, rapid rotation, tiny motion, long,
and nonuniform clips.

This is explainable key reduction, not globally optimal compression. No entropy
coding, SIMD, or 16-bit quantization is claimed. The committed report shows static
704→64 bytes and the long case 16,448→1,136 bytes within configured limits.
