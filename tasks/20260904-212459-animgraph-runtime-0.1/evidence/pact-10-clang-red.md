# PACT-10 Clang RED evidence

- CI run: `33880207215`
- Commit: `0ee37d7626622b8b8050aa7a7b0d74aae3655976`
- Windows/MSVC: passed.
- Ubuntu/GCC 13.3.0: passed.
- Ubuntu/Clang 18.1.3: build exited 1 because its default standard-library
  combination did not expose `std::expected` under C++23.

The official Ubuntu 24.04 runner manifest for image `20260831.293.1` lists Clang
16/17/18 and GCC 12/13/14, with no listed libc++ package. The portability repair
therefore keeps `std::expected` as the preferred implementation and adds a narrow
`animgraph::Expected` compatibility value only when `__cpp_lib_expected` is absent.
