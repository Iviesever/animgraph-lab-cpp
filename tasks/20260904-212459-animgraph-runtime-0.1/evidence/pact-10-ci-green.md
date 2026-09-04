# PACT-10 cross-compiler GREEN evidence

- Workflow run: `33880547747`
- URL: `https://github.com/Iviesever/animgraph-lab-cpp/actions/runs/33880547747`
- Commit: `83efbc45eaf54f4530649676619e38896208af42`
- Windows/MSVC: success in 2 minutes 2 seconds.
- Ubuntu/Clang 18.1.3: success in 25 seconds after the conditional Expected repair.
- Ubuntu/GCC 13.3.0: success in 8 seconds using native `std::expected`.

All jobs configured, built the library/CLI/tests, and passed CTest. The public API
prefers the standard C++23 expected type where exposed and preserves identical
value/error semantics through the narrow fallback on Clang's default library.
