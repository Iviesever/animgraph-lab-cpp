# Verification matrix

| Area | Pre-change RED | Green evidence | Final gate |
|---|---|---|---|
| Baseline | Version API/header absent | MQB CLI/test + CMake/CTest | main pushed |
| Math/Pose | Math/skeleton contract tests fail | Unit + oracle + bounds | MSVC/Clang |
| Clip/Asset | Seam/parser tests fail | Round-trip + invalid corpus | fuzz/sanitizer |
| Compression | Error-bound tests fail | Grid/random oracle report | ratio/error report |
| Graph | Validation/slot tests fail | Plan and runtime suites | identity repeat |
| Blend/State | Transition/oracle tests fail | Blend/state/root suite | event order repeat |
| IK/Batch | Solver/order tests fail | IK + serial/parallel suite | race isolation |
| Trace/Viewer | CLI/viewer tests fail | real trace + browser QA | screenshot/console |
| Package | clean extraction absent | verify/evaluate/viewer commands | SHA/manifest/HEAD |
| Delivery | audit/PR absent | two audits + Draft PR | clean pushed branch |

Every evidence record includes command, timestamp, exit code, toolchain, test count,
and relevant output. A missing compiler is recorded as an environmental gap and is
not reported as a pass.
