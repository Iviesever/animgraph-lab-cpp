# PACT-30 GCC RED evidence

- CI run: `33882786187`
- Commit: `1d1bb5a6f31a6ec1d98fc72d5ccbc6e6a79a555c`
- Windows/MSVC and Ubuntu/Clang: success.
- Ubuntu/GCC: test translation unit rejected under `-Wdangling-reference -Werror`.
- Root cause: a test macro bound references to `.value()` subobjects of temporary
  `Expected` objects in the byte-stability assertion.

The repair stores both encoded Expected values in named locals, verifies success,
then compares the owned byte vectors. Production compression behavior is unchanged.
