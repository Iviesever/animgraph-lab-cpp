# Local package GREEN evidence

- Source/build HEAD: `9bb079193209baab09c9d728424ddad4b0a3852a` (clean).
- CMake/MSVC Release before packaging: 3/3 CTest passed in 0.86 seconds.
- Win64 ZIP: `artifacts/release/AnimGraphLab-Win64-0.1.0-9bb07919.zip`
  - Size: 314305 bytes
  - SHA-256: `a457877874685c38c5da7baae9e96eb7acba22d06c37786fedeafcf48f81752b`
- Source ZIP: `artifacts/release/AnimGraphLab-Source-0.1.0-9bb07919.zip`
  - Size: 230842 bytes
  - SHA-256: `d532b483a3dda3888dd66f75ecf33664838ea193d22ac4c3ae66ff748daeb74b`
- External manifest: `artifacts/release/DELIVERY_MANIFEST.json`.
- Both adjacent `.sha256` files match the manifest.

Win64 content includes both executables, sample Skeleton/Clip/Graph/Trace, sample
Viewer, README, packaged Quick Start, License, and internal Manifest.

Clean extraction command:

```text
scripts/verify_release.ps1 -Package artifacts/release/AnimGraphLab-Win64-0.1.0-9bb07919.zip
```

Result: exit 0. `animgraph_lab verify`, `evaluate --sample locomotion`, and
`generate-viewer` all succeeded without the development tree. Generated extraction
Trace was 212681 bytes and Viewer 218249 bytes. The validated extraction directory
was removed afterward; all persistent evidence remains under this repository.
