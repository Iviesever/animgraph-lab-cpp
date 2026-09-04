# PACT-70 local GREEN evidence

- MQB/MSVC: 45/45 tests passed.
- CMake/MSVC: CTest 1/1 passed in 2.70 seconds.
- Source drift: 17 library sources match; MQB CLI discovery builds 18 TUs including main.
- CLI `verify`: success, 15 graph instructions.
- CLI `sample`: 15-joint humanoid, 6 named clips, 15 instructions.
- CLI `evaluate`: 60 real frames.
- CLI `generate-viewer`: self-contained HTML, no HTTP/CDN references.
- CLI `benchmark`: 9 observed rows.
- Cross-compiler CI: run `33886878359` passed MSVC (52s), Clang (33s), GCC (30s)
  for exact code SHA `4c166f38671bed013deba8960f4d055d2fba147d`.

Artifacts generated from that exact code SHA:

- `samples/trace/locomotion.trace.json`, 212751 bytes, SHA-256
  `8DAF2297BF2B3A67A80B10B9169ADC596030B90310383122D5D8435DB57FF5EA`.
- `viewer/animgraph_debugger.html`, 218319 bytes, SHA-256
  `FA3BA6686CB6A4592BC637101B4CBD039D4BF5965409C517F117AAF34ECD9A92`.
- `artifacts/reports/benchmark.json`, SHA-256
  `51141EBCC6B571239F9CA3A77D0D7610F810832473BF451C745AB0996D556649`.

Observed Debug benchmark samples (local machine, no cross-machine SLA):

- Complex graph, 1/100/1000 characters: 433000 / 404744 / 342389 ns/character.
- Clip-only: 642.22 ns/joint across 1000 samplings.
- Raw + compressed sampling: 565.4 ns/joint across 200 samplings.
- Serial 1000: 334090 ns/character.
- Batch parallel 1000, 4 workers: 281701 ns/character.

Browser QA status: the in-app browser security policy rejected the local `file://`
artifact and explicitly prohibited localhost, alternate browser, or command-line
workarounds. No browser screenshot or interactive-console claim is made. Static
tests verify all required controls, embedded real Trace, responsive breakpoint,
and absence of network dependencies; actual browser QA remains a completion gap.
