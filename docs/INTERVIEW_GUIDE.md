# Interview guide

## Core explanations

- **Local / Model / Skin:** Local is relative to parent; Model is accumulated from
  root; Skin combines Model with inverse bind for vertex deformation.
- **Why quaternions:** Four values compactly represent 3D rotation without Euler
  gimbal lock and support smooth interpolation/composition.
- **NLerp / SLerp:** NLerp is cheaper and normalized but non-constant angular speed;
  SLerp follows the sphere at constant angular speed. This sampler uses SLerp.
- **`q` and `-q`:** They encode the same rotation; flip the second when dot is
  negative to take the shortest interpolation path.
- **Loop seam:** Exact duration maps to zero for looping samples; interval queries
  split seams and use `(from,to]` to avoid duplicate events.
- **Blend tree:** Maps parameters to weighted source Poses; weights remain bounded
  and normalized.
- **Additive Pose:** Stores delta from a reference and applies a weighted translation,
  scale, and rotation delta to a base.
- **Layer mask:** Per-joint weights propagated through Skeleton descendants combine
  a layer without replacing unrelated joints.
- **Why compile a Graph:** Validation, stable schedule, compact state, DCE, slot
  reuse, identity, and observability happen once instead of every frame.
- **Pose Slot lifetime:** A slot is reusable only after its producer's last consumer;
  a reuse-disabled plan is the oracle.
- **Transition ownership:** Source and target remain explicit until alpha reaches one;
  an interruptible target becomes the next transition's source.
- **Sync Marker:** A named phase landmark maps normalized offset between clips.
- **Root Motion:** Runtime emits a Transform delta; gameplay decides how to apply it.
- **Two-Bone IK:** Law of cosines finds a bend point; pole chooses the plane; only
  Root/Mid rotations change.
- **Pose Cache invalidation:** Frame, generation, and parameter hash must all match;
  cache storage is per GraphInstance.
- **Compression error:** Position/scale use distances, rotation uses radians; raw
  versus reduced sampling proves thresholds.
- **Why parallelize by character:** Characters have disjoint instances and scratch;
  this avoids shared Pose writes and preserves deterministic ordering.
- **Why no bitwise pose determinism:** IEEE float operations and optimizations differ
  across CPUs/compilers; semantic assets/plans are stable and poses use tolerances.
- **UE integration:** An AnimInstance adapter would map parameters and consume the
  output Pose/events/root delta; this library deliberately owns no UE types.
- **AI authorship:** The user chose direction/scope/constraints; Codex authored the
  delivery code, tests, debugging, reports, Viewer, packaging, and documents.

Before an interview, complete at least one drill and explain Skeleton, Clip, Graph,
State, Root Motion, IK, and Compression without reading this page.
