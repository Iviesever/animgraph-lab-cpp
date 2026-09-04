# Technical blueprint and implementation plan

The user-supplied goal is the approved product blueprint. Delivery is divided into
independently verifiable PACTs so every checkpoint can be recovered, reviewed, and
reverted without invalidating earlier evidence.

## PACT-00 — Baseline and contract

Create the repository rules, contract, build files, minimal version API/CLI test,
CI skeleton, and source-set drift guard. Witness a missing-version RED test, then
build and run via MQB and CMake/CTest. Push this verified minimum to `main`, fetch
`origin/main`, and branch `feat/animgraph-runtime-0.1` from that exact revision.

## PACT-10 — Math, skeleton, and pose

Test first: finite math validation, quaternion endpoint/antipode/180-degree cases,
compose/inverse, skeleton roots/cycles/order/bounds, and local/model/skin oracles.
Implement focused math and skeleton value types, then retain MQB/MSVC and portable
regression evidence.

## PACT-20 — Clip, time, events, markers, assets

Test time normalization and loop seams before adding integer `AnimTime`, tracks,
sampling, interval event queries, and marker ordering. Test malformed bytes before
adding explicit `.agskel` and `.agclip` version-1 codecs plus compile/inspect/validate
CLI operations. Add deterministic round-trip and bounded fuzz entry points.

## PACT-30 — Compression

Define raw-versus-compressed oracle grids and error thresholds in failing tests.
Implement constant detection and stable linear key reduction for translation,
rotation-angle, and scale error metrics. Record counts, byte estimates, maxima, and
reports for the required procedural clips without claiming global optimality.

## PACT-40 — Graph compiler and runtime

Test typed wiring, missing inputs, cycles, dead nodes, stable plan JSON/identity,
state layout, last-use slot reuse, and reuse-disabled equivalence. Implement a
compact tagged instruction plan and per-character instance evaluator. Test cache
hits, generation invalidation, parameter changes, and character isolation.

## PACT-50 — Blend, state, and root motion

Use explicit oracle tests for Blend1D ordering/clamp, bounded Blend2D barycentrics,
additive rotation deltas, hierarchy masks, stable transition priority/interrupt,
sync-marker alignment, crossfade event policy, and root-motion seams. Implement
only the contracts established by those tests.

## PACT-60 — IK and batch evaluation

Test reachable, extended, too-close, degenerate, pole fallback, weight, preserved
length, and isolated chain edits. Test serial output as an oracle before adding a
bounded joining `std::jthread` worker pool with stable result/event order and stop
support. Keep character scratch and instances disjoint.

## PACT-70 — CLI, trace, viewer, verification

Build the procedural humanoid and locomotion graph through the same runtime APIs.
Emit a versioned real trace, benchmark matrix, and self-contained HTML viewer.
Exercise CLI commands and browser controls, check console output and narrow layout,
capture a screenshot, and store all evidence in the repository.

## Cross-cutting verification and delivery

Run deterministic 10,000-valid/10,000-invalid property sweeps and 100,000 bounded
fuzz inputs; MSVC Debug/Release; Clang and GCC where installed; ASan/UBSan in a
supported environment; benchmark samples; and source-list drift checks. Generate
Win64 and source ZIPs plus manifest and SHA-256 at one clean HEAD, extract to a new
temporary directory inside the repository, run the three required commands, then
remove only the verified extraction directory. Complete two non-overlapping
read-only audits, fix confirmed Blocker/High issues through RED/GREEN tests, update
truthful docs, push, and create—but do not merge—the Draft PR.

## Conditional work

Motion Matching starts only when every P0 gate and packaging gate is green before
2026-09-05 10:30 JST and no freeze has been announced. It is abandoned cleanly
after two unsuccessful repair cycles and cannot delay P0 delivery.

## Recovery

After each PACT, `progress.md`, `reset_handoff.md`, exact command logs, test totals,
performance data, and exact HEAD are updated before an independent commit and push.
The latest pushed PACT commit is the last-known-good recovery point.

## Blueprint self-review

- Placeholder scan: no unresolved placeholders or deferred implementation markers.
- Consistency: immutable graph/assets, per-instance mutation, and single evaluator
  match the product and verification contracts.
- Scope: every required P0 subsystem maps to exactly one primary PACT; conditional
  Motion Matching is isolated.
- Ambiguity resolution: format version 1, little endian, CRC32, bounded stable IDs,
  FNV-1a plan identity, and hierarchy-mask propagation are explicit choices.
