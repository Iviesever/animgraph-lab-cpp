# Batch evaluation

`EvaluationJob` binds CharacterId, immutable context/graph, and one mutable
GraphInstance. The serial path is the reference. Parallel evaluation stable-sorts
jobs by CharacterId, rejects null or reused instance pointers, and assigns complete
characters through an atomic bounded queue.

Workers are `std::jthread`; no thread detaches. The implementation explicitly joins
every worker before returning (a destructor-stop race was caught by tests). A
pre-requested external stop returns typed cancellation, and a mid-run stop leaves
unclaimed results cancelled. Tests compare serial and worker counts 1/2/4 and repeat
the suite 25 times. Parallelism never writes one Pose from multiple jobs.
