# Audit cycle 2 GCC RED

- CI run: `33894201504`
- Commit: `e492426558ac4187b5a958bd043958fcaba55988`
- MSVC, Clang, and Clang ASan+UBSan: success.
- GCC: two `-Wmisleading-indentation -Werror` diagnostics in the canonical node
  config writer where an unconditional write shared the line with a comma guard.

The repair separates the unconditional write onto its own line. Plan bytes and
runtime behavior are unchanged.
