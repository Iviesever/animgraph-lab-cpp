# PACT-00 CI GREEN evidence

- Workflow: `ci`
- Run ID: `33879239638`
- URL: `https://github.com/Iviesever/animgraph-lab-cpp/actions/runs/33879239638`
- Tested commit: `684d663263bcbae593351fe577f86a4fa542aa73`
- Overall result: success

Jobs:

- Windows/MSVC: success in 30 seconds; checkout, repository-local vcvars/Ninja
  configure/build, and CTest all passed.
- Ubuntu/Clang: success in 40 seconds; configure, build, and CTest all passed.
- Ubuntu/GCC: success in 8 seconds; configure, build, and CTest all passed.

All three jobs compiled the same `cmake/AnimGraphSources.cmake` source manifest and
ran the dependency-free baseline test executable with 1/1 passing test.
