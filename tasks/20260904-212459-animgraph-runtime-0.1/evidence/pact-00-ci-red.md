# PACT-00 CI RED evidence

Run: `33879018719`

- Windows/MSVC configure exited 1 because CMake 4.1 on the hosted runner did not
  recognize its installed Visual Studio 18 as a `Visual Studio 17 2022` instance.
- Ubuntu Clang 18.1.3 configured successfully, then build exited 1 because the
  workflow passed configure preset `ninja-clang-debug` where the file had named the
  build preset `clang-debug`.
- Ubuntu GCC 13.3.0 had the corresponding `ninja-gcc-debug` / `gcc-debug` mismatch.

The failures are CI integration failures, not product-test failures. The repair
uses the already verified repository-local vcvars/Ninja script on Windows and
identical configure/build/test names on Linux.
