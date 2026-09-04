# Product contract

## Objective

Deliver a self-contained, engine-independent C++23 skeletal-animation runtime and
compiled graph lab whose real behavior is observable through tests, traces,
benchmarks, CLI tools, and an interactive offline HTML debugger.

## Acceptance summary

- All PACT-00 through PACT-70 capabilities are executable and covered by unit,
  integration, property, fuzz, portability, and clean-package verification.
- Stable artifacts include versioned skeleton/clip codecs, canonical graph plans,
  stable identities, deterministic ordering, and bounded fail-closed parsing.
- Runtime output is finite and tolerance-consistent; serial and parallel character
  evaluation preserve input order and instance isolation.
- The viewer consumes an actual emitted trace and passes browser interaction QA.
- Evidence, reports, packages, SHA-256 files, branch commits, push, and Draft PR all
  bind to a clean final revision.

## Non-goals

Unreal/Unity integration, rendering APIs, GPU skinning, FBX/glTF import, a full
editor, cloth, ragdoll, full-body IK, retargeting, networking, an ECS framework,
custom generic containers, and a binary GitHub release are excluded.

## Determinism statement

Identical asset bytes, parameters, and binary yield stable repeatable behavior.
Asset compilation, graph plans, event order, and versioned files are byte-stable.
Floating-point poses are verified with declared tolerances across compilers; the
runtime does not claim cross-platform bitwise pose determinism or rollback-lockstep
suitability.

## Authorization boundary

Local and remote repository creation, public visibility, feature branches,
commits, pushes, local builds/tests/artifacts, and a Draft PR are authorized.
Merge, tags, release publication, remote-branch deletion, and uploaded binaries
require new explicit authorization.
