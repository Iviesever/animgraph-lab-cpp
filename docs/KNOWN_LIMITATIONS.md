# Known limitations

- No Unreal/Unity adapter, renderer, GPU skinning, importer, retargeting, full-body
  IK, cloth, ragdoll, networking, or ECS integration.
- Blend2D uses exactly one triangle and projects outside points to nonnegative
  normalized weights; it is not a triangulated navigation mesh.
- The sample Graph uses global ordered parameter slots for several demo nodes rather
  than per-node named binding metadata.
- Additive Root Motion intentionally uses base-only policy.
- The Clang 18 default standard library lacks `std::expected`; a narrow internal
  compatibility value is used there, while MSVC/GCC use the standard type.
- Allocation counts are not reported because no trustworthy global allocator hook
  was installed. Runtime sizes are bounded, but output value copies may reuse or
  acquire vector capacity.
- FNV-1a Graph identity detects stable-plan changes; it is not cryptographic.
- Benchmark numbers are local observations and Debug values are not shipping SLAs.
- Browser security policy blocked local Viewer loading; no interactive console-zero
  or screenshot claim is made despite static HTML/Trace tests.
- No cross-compiler bitwise Pose determinism or rollback-lockstep claim.
