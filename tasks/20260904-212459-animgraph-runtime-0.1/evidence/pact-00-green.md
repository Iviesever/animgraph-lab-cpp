# PACT-00 local GREEN evidence

Time: 2026-09-04 21:35 CST

## MQB 5.4.0 / MSVC

Core test command:

```text
mqb run tests/test_main.cpp tests/version_tests.cpp src/core/version.cpp
  --profile tests-debug --std 23 -j 4 -I include -I tests
  /EHsc /W4 /WX /permissive- /utf-8 --timings=json
```

Result: exit 0, 1/1 test passed. Repeat result: exit 0 with 3 compile
cache hits, 0 misses, 1 link cache hit, and total 23.919 ms.

CLI command: `mqb run --profile debug -j 4 --timings=json -- verify`

Result: exit 0, 2 production translation units discovered, output
`AnimGraphLab baseline verification: ok`. MQB reported MSVC compiler
`19.51.36248.0` from Visual Studio 18 Community.

## CMake / CTest / MSVC

Command: `scripts/run_cmake_msvc.ps1 -Configuration Debug`

Result: exit 0. CMake 4.1.2 used Ninja and MSVC 19.51.36248.0 through the
detected `vcvars64.bat`; 7 build steps completed and CTest passed 1/1 tests.

The initial Visual Studio 17 generator configure exited 1 because the machine
contains Visual Studio 18, which this CMake version does not expose as a generator.
The repository-local script provides the verified local CMake/MSVC boundary without
changing system configuration.

## Build ownership

Command: `scripts/check_source_drift.ps1`

Result: exit 0, `Source manifests match (1 source).`

## Cross-compiler status

No local WSL, Clang, or GCC environment is installed. The committed GitHub Actions
matrix runs Windows MSVC, Ubuntu Clang, and Ubuntu GCC; its run is the remaining
PACT-00 gate after the baseline push.
