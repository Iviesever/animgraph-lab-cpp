# Final audit regression cycle

Date: 2026-09-05 CST  
Candidate parent: `c8706f551615de354bffe3bf8086085e9fbf0728`

## Witnessed RED

`scripts/run_cmake_msvc.ps1 -Configuration Debug` initially stopped compiling the
test target because `EvaluationResult` had no `sync_marker_occurrences`. After the
first implementation pass, the new delayed marker-sync case failed with the target
clock at 15 instead of a synchronized absolute tick. Both failures preceded the
corresponding production changes.

## Confirmed fixes

- Event and marker occurrences propagate through pose slots, retain provenance,
  stable-deduplicate DAG reconvergence, and exclude zero-contribution layer/state
  branches.
- State updates carry the exact selected transition index. Marker synchronization
  runs before clip sampling, binds an unambiguous compiled player, maps local time
  to a monotonic absolute clock, and treats the jump as an event/root discontinuity.
- Exit Time 0 is accepted at state entry.
- Rotation reduction is proved at every discrete tick for spans up to 100,000;
  candidates outside the bounded proof or tolerance retain original keys.
- `.aggraph` decoding uses a non-recursive exact canonical-schema parser with
  typed configs, arity, slots, state layout, sync binding, numeric, and count checks.
- Duplicate ValuePin bindings, degenerate Blend2D triangles, invalid IK limits,
  and corrupted runtime instruction shapes fail closed.
- The official MQB wrapper injects a 40-character Git SHA; both CLI tools expose it.
- Source packaging overlays final-SHA Trace/Viewer/Benchmark, adds CMake revision
  metadata, and requires clean-extraction Release build/CTest verification.

## Green commands

```text
scripts/run_cmake_msvc.ps1 -Configuration Debug
exit 0; 67/67 unit/integration, property 10000/10000 + batch 1000,
fuzz 100000; CTest 3/3; 54.86 s

scripts/run_mqb_tests.ps1 -Configuration Release
exit 0; 65/65 at the first audit pass; wrapper-revision output was
sha=c8706f551615de354bffe3bf8086085e9fbf0728

scripts/run_cmake_msvc.ps1 -Configuration Release
exit 0; 67/67 unit/integration, property 10000/10000 + batch 1000,
fuzz 100000; CTest 3/3; 5.46 s
```

The tracked Trace, Viewer, and Benchmark snapshots were regenerated from the
Release candidate binary after the final code changes. Delivery archives are
regenerated only after the candidate commit so their manifests can bind final HEAD.

## Independent review

The Math/Graph/Compression reviewer and Tests/Viewer/Packaging reviewer both
performed final read-only follow-ups and reported zero remaining Blocker/High.
Browser interaction remains separately unverified because the permitted browser
surface rejected local `file://` content and explicitly prohibited workarounds.
