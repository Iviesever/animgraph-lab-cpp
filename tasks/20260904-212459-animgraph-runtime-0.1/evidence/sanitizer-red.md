# Sanitizer RED evidence

- CI run: `33887735653`
- Commit: `d472c749965a3b3bd720102163d5d9120bbac3e6`
- Regular MSVC, Clang, and GCC jobs: success.
- Clang ASan+UBSan: build succeeded; Property and Fuzz passed; unit/integration
  executable failed with `AddressSanitizer: stack-use-after-scope` at
  `tests/clip_asset_tests.cpp:43`.
- Root cause: `AG_CHECK_EQ` bound const references to subobjects returned through a
  temporary Expected expression.

The repair captures both assertion operands by value. Production runtime code is
unchanged and sanitizer options remain enabled at halt-on-error strength.
