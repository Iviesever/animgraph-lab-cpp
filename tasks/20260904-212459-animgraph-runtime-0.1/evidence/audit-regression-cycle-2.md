# Independent audit regression cycle 2

This checkpoint addresses the core configuration, trace/viewer, property/fuzz,
benchmark, package-integrity, and artifact-freshness findings from both independent
read-only audits.

Implemented:

- typed NodeConfig variant and compiled parameter indices for every configurable P0
  node; ValuePin named bindings; Runtime uses compiled thresholds, triangle, mask,
  StateMachineDefinition, root policy, and IK chain/pole/limit;
- State exit interval crossing and marker synchronization in the compiled runtime;
- active-branch event propagation with absolute Tick/cycle/source occurrence data;
- evaluation memo before upstream time side effects and Runtime root accumulator;
- Runtime-produced executed nodes, state/transition, blend, marker, root, event, and
  Foot/Hand IK observations copied directly to Trace;
- Base64-safe self-contained Viewer with escaped dynamic text, hierarchy-correct
  local visualization, IK overlays, and compression canvas;
- distinct State/IK/Clip/raw/compressed benchmark paths and separate raw/reduced times;
- varied Property topology/data with per-case execution/identity Oracle and nine
  typed invalid categories; Fuzz valid/invalid Oracles across twelve mutation classes;
- compiler-bound Git SHA, package binary/HEAD check, staging regeneration, complete
  package Manifest/SHA/content verification, and expanded CI source drift gate.

GREEN evidence at this checkpoint:

- MQB/MSVC Debug: 58/58 named tests.
- MQB/MSVC Release Property: valid=10000, invalid=10000, batch_cases=1000.
- MQB/MSVC Release Fuzz: 100000 total; 8333 verified valid, 83333 verified invalid,
  8334 random; all three formats and recomputed-CRC paths.
- CMake/MSVC Debug: 3/3 targets passed in 32.42 seconds.
- Source ownership: 17 library sources, 11 unit/integration sources, 4 entry points.

Remaining external gap: browser URL policy still prevents the mandated local-file
interactive/browser screenshot check. No workaround or passing claim is made.
