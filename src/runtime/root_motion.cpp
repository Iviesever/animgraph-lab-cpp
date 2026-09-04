#include "animgraph/runtime/root_motion.hpp"

#include <algorithm>

namespace animgraph {
namespace {

Expected<Transform, Error> sample_root(const CompiledSkeleton& skeleton,
                                       const AnimationClip& source, JointId root,
                                       AnimTime local) {
  AnimationClip clip = source;
  clip.mode = ClipPlaybackMode::clamp;
  const auto sampled = sample_clip(skeleton, clip, local);
  if (!sampled) return make_unexpected(sampled.error());
  if (root.value >= sampled->pose.transforms.size())
    return make_unexpected(Error{ErrorCode::bounds, "root motion joint is invalid"});
  return sampled->pose.transforms[root.value];
}

Expected<Transform, Error> segment_delta(const CompiledSkeleton& skeleton,
                                         const AnimationClip& clip, JointId root,
                                         AnimTime from, AnimTime to) {
  const auto start = sample_root(skeleton, clip, root, from);
  const auto finish = sample_root(skeleton, clip, root, to);
  if (!start) return make_unexpected(start.error());
  if (!finish) return make_unexpected(finish.error());
  const auto inverse_start = inverse(*start);
  if (!inverse_start) return make_unexpected(inverse_start.error());
  return compose(*inverse_start, *finish);
}

}  // namespace

Expected<Transform, Error> extract_root_motion(
    const CompiledSkeleton& skeleton, const AnimationClip& clip, JointId root,
    AnimTime from, AnimTime to) {
  if (root.value >= skeleton.joints.size() || to < from)
    return make_unexpected(Error{ErrorCode::invalid_argument, "root motion interval is invalid"});
  const auto valid = validate_clip(clip);
  if (!valid) return make_unexpected(valid.error());
  if (clip.duration.ticks == 0 || from == to) return Transform::identity();
  if (clip.mode != ClipPlaybackMode::loop) {
    const auto start = normalize_time(from, clip.duration, ClipPlaybackMode::clamp).value().local;
    const auto finish = normalize_time(to, clip.duration, ClipPlaybackMode::clamp).value().local;
    return segment_delta(skeleton, clip, root, start, finish);
  }
  Transform total = Transform::identity();
  std::int64_t cursor = from.ticks;
  std::size_t segments = 0;
  while (cursor < to.ticks && segments++ < 4096) {
    const auto normalized = normalize_time(AnimTime{cursor}, clip.duration, ClipPlaybackMode::loop).value();
    const std::int64_t remaining = clip.duration.ticks - normalized.local.ticks;
    const std::int64_t advance = std::min(remaining, to.ticks - cursor);
    const auto delta = segment_delta(skeleton, clip, root, normalized.local,
                                     AnimTime{normalized.local.ticks + advance});
    if (!delta) return make_unexpected(delta.error());
    total = compose(total, *delta);
    cursor += advance;
  }
  if (cursor != to.ticks)
    return make_unexpected(Error{ErrorCode::bounds, "root motion interval exceeds segment limit"});
  return total;
}

void apply_root_motion_policy(LocalPose& pose, JointId root,
                              RootMotionPosePolicy policy) noexcept {
  if (policy == RootMotionPosePolicy::remove && root.value < pose.transforms.size())
    pose.transforms[root.value] = Transform::identity();
}

Transform blend_root_motion(const Transform& a, const Transform& b, float weight) noexcept {
  const float clamped = std::clamp(weight, 0.0F, 1.0F);
  return {a.translation + (b.translation - a.translation) * clamped,
          slerp(a.rotation, b.rotation, clamped),
          a.scale + (b.scale - a.scale) * clamped};
}

}  // namespace animgraph
