# Benchmarking

The benchmark records characters, joints, nodes, pose evaluations, total nanoseconds,
ns/character, ns/joint, cache hit rate, scratch slots, raw/compressed bytes, maximum
errors, workers, compiler, CPU environment text, and OS. It makes no cross-machine SLA.

Scenarios include 1/100/1,000-character complex graph, true ClipSampler-only work,
state and IK-enabled graph runs, raw+compressed sampling, serial 1,000, and 4-worker
batch 1,000. The committed local MSVC Debug observation measured 334,090 ns/character
serial versus 281,701 ns/character at four workers. This is a local observation, not
a universal speedup guarantee. No SoA/SIMD/scratch allocator optimization was added
because this cycle prioritizes measured correctness and observability.
