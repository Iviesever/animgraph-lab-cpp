# PACT-20 cross-compiler RED evidence

- CI run: `33881995134`
- Commit: `337aca11c27bddaac8e074bc0743b323d8e6d315`
- Windows/MSVC: success in 56 seconds.
- Ubuntu/Clang and Ubuntu/GCC: build exited 1 under warnings-as-errors.
- Root cause: aggregate designated initializers intentionally omitted trailing
  vector members in `AnimationClip` and `JointTrack`; both compilers emitted
  `-Wmissing-field-initializers`.

The repair value-initializes complete objects and then assigns named scalar fields,
preserving behavior while making initialization explicit on every toolchain.
