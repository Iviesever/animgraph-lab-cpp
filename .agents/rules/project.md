# Project rules

## Scope and safety

- Repository root is exactly `D:\program\AnimGraphLab`.
- Do not write outside this repository during delivery.
- Do not alter FrameGraphLab, SeedForge, RollbackLab, MQB, Unreal Engine, or any
  sibling repository.
- Do not terminate unknown processes. Use bounded build parallelism.
- Never persist credentials or include machine secrets in evidence.

## Engineering contract

- C++23, standard-library-first, engine-independent, data-oriented design.
- Asset and compiled graph objects are immutable value data; mutable execution
  belongs to a per-character instance.
- Use strong stable IDs, bounded counts, checked conversions, finite-float
  validation, and fail-closed parsers.
- Runtime schedules come only from the compiler-produced instruction plan.
- Hot paths must not perform unbounded allocation.
- Float pose results are tolerance-stable, not claimed bit-identical across
  compilers or architectures. Asset bytes, graph plans, identities, and event
  ordering must be stable for identical inputs.

## Workflow

- Each PACT starts with acceptance checks and a witnessed RED state.
- Each PACT ends with raw evidence, updated progress/handoff, an independent
  commit, and a push.
- Keep CMake and MQB source sets synchronized through one authoritative list or
  an automated drift check.
- Only verified capabilities may appear in README or release-facing documents.
- Before completion, run independent read-only audits and verify a clean tree.

## Publication boundary

- Public repository and Draft PR are authorized.
- Merge, remote-branch deletion, tags, releases, and binary uploads are forbidden
  without later explicit authorization.
