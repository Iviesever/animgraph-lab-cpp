# AnimGraphLab 0.1.0 candidate notes

This source-only candidate introduces a C++23 skeletal-animation runtime with all
ten P0 graph nodes, integer-time Clips, events/markers, three versioned asset
formats, deterministic compression, Root Motion, Two-Bone IK, stable batch
evaluation, real Trace output, an offline Viewer, benchmarks, and full verification
harnesses.

Verified locally with MQB/MSVC and CMake/MSVC Debug/Release; CI covers MSVC,
Clang, GCC, ASan, and UBSan. Property totals are 10,000 valid + 10,000 invalid;
Fuzz totals 100,000 inputs. See `KNOWN_LIMITATIONS.md` for the browser QA gap and
other explicit boundaries.

No tag, GitHub Release, merge, or binary upload is part of this candidate. A future
authorized release should use annotated `v0.1.0`, GitHub-generated source archives
only, and these notes after final factual review.
