#pragma once

#include "animgraph/clip/clip.hpp"
#include "animgraph/core/expected.hpp"

namespace animgraph {

enum class RootMotionPosePolicy { keep, remove };

[[nodiscard]] Expected<Transform, Error> extract_root_motion(
    const CompiledSkeleton& skeleton, const AnimationClip& clip, JointId root,
    AnimTime from, AnimTime to);
void apply_root_motion_policy(LocalPose& pose, JointId root,
                              RootMotionPosePolicy policy) noexcept;
[[nodiscard]] Transform blend_root_motion(const Transform& a, const Transform& b,
                                          float weight) noexcept;

}  // namespace animgraph
