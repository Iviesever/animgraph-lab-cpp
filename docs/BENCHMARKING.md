# Benchmarking

The benchmark records characters, joints, nodes, pose evaluations, total nanoseconds,
ns/character, ns/joint, cache hit rate, scratch slots, raw/compressed bytes, maximum
errors, workers, compiler, CPU environment text, and OS. It makes no cross-machine SLA.

Scenarios include 1/100/1,000-character complex graph, true ClipSampler-only work,
standalone State transition work, standalone IK work, separately timed raw and
compressed sampling, serial 1,000, and 4-worker batch 1,000. Final values are
regenerated from the Release binary during packaging. They are local observations,
not universal speedup guarantees. No SoA/SIMD/scratch allocator optimization was
added because this cycle prioritizes measured correctness and observability.
