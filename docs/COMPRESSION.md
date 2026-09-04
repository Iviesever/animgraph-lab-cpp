# Compression

Compression first detects tracks whose values remain inside the declared error of
the first key. Non-constant tracks use deterministic maximum-error splitting: keep
endpoints, find the first largest deviation from endpoint interpolation, split when
the threshold is exceeded, and repeat. There is no hash-order dependence.

Translation/scale errors are Euclidean distances; rotation error is quaternion
angular distance. Reports contain raw/compressed keys and estimated bytes plus the
three maxima per joint and globally. Raw and reduced clips are compared on uniform
and deterministic random times for static, walk, rapid rotation, tiny motion, long,
and nonuniform clips. Rotation candidates are certified at every representable
integer animation tick for spans up to 100,000 ticks, which exactly matches the
runtime's discrete `AnimTime` domain. If the span exceeds that bounded offline proof
budget or any tick exceeds tolerance, the original rotation keys are retained. The
reported maximum is therefore exhaustive for a reduced path, or zero for an
unchanged path.

This is explainable key reduction, not globally optimal compression. No entropy
coding, SIMD, or 16-bit quantization is claimed. The committed report shows static
704→64 bytes and the long case 16,448→1,136 bytes within configured limits.
