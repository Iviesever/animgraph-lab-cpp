# Clip sampling

Animation time is signed 64-bit ticks at 48,000 ticks/second. Runtime sampling never
reads wall clock. Clamp, Loop, and PingPong normalization define negative, large,
zero-duration, exact-end, and loop-seam behavior.

Translation and scale use linear interpolation. Rotation uses shortest-path SLerp.
Empty component tracks fall back to the compiled Skeleton reference transform; a
missing joint track leaves the entire reference joint unchanged. Single keys remain
constant, and output joint count always matches the Skeleton.

Events use the half-open logical interval `(from,to]`, including split loop seams,
which prevents duplicate boundary delivery. Events and sync markers must be strictly
sorted by time/name (events also use payload as a final ordering key). Marker mapping
preserves normalized offset from the named marker between source and target clips.
