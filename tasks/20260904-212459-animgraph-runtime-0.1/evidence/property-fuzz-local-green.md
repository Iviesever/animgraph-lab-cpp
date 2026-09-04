# Property, fuzz, and MSVC Release GREEN evidence

## MQB/MSVC Release

- Property: `PROPERTY valid=10000 invalid=10000 batch_cases=1000 batch_match=true`.
- Fuzz: `FUZZ inputs=100000 max_bytes=1024 deep_crc=true formats=3`.
- Fuzz formats: `.agskel`, `.agclip`, `.aggraph`.
- Deep cases include recomputed CRC after oversized counts, invalid offsets, and
  non-finite transform injection, plus truncation and bounded random bytes.

## CMake/Ninja/MSVC Debug

- 12 build steps.
- `animgraph_tests`: passed in 4.53 seconds (46 tests after Graph asset addition).
- `animgraph_property`: passed in 6.24 seconds.
- `animgraph_fuzz`: passed in 3.32 seconds.
- CTest: 3/3 passed, 14.09 seconds total.

## CMake/Ninja/MSVC Release

- 37 clean build steps with MSVC 19.51.36248.0.
- `animgraph_tests`: passed in 0.41 seconds.
- `animgraph_property`: passed in 0.57 seconds.
- `animgraph_fuzz`: passed in 0.42 seconds.
- CTest: 3/3 passed, 1.41 seconds total.

Clang ASan/UBSan is configured as a separate Ubuntu CI job because no local Clang
toolchain is installed; its result remains pending at this checkpoint.
