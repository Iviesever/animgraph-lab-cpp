# PACT-50 local GREEN evidence

- MQB/MSVC: 35/35 tests passed.
- Repetition: the same runtime binary passed 10/10 full executions.
- CMake/MSVC: 10 incremental steps; CTest 1/1 passed in 0.11 seconds.
- Source drift: 12 production sources match.

Verified behavior:

- Blend1D stable sorts thresholds, clamps outside values, returns exact samples, and
  rejects duplicates.
- Blend2D uses one explicit triangle, rejects degeneracy, projects weights to the
  nonnegative simplex, and keeps their sum within tolerance of one.
- Additive rotation uses `inverse(reference) * additive`; layered masks propagate
  down the compiled hierarchy and validate every weight.
- Transition choice is priority-first then target-StateId; exit time, zero duration,
  one-shot exit/enter events, and explicit interrupt policy are tested.
- Named marker synchronization maps normalized marker offset between clips.
- Root motion emits Transform deltas, splits loop seams, never mutates world state,
  and exposes explicit pose keep/remove policy. Additives retain base root motion;
  blend/layer/state nodes blend contributing deltas.
- Crossfade event policy evaluates all contributing players and stable-deduplicates
  identical `(time,name,payload)` events at final output.
