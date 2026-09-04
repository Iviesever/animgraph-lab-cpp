#pragma once

#include "animgraph/core/expected.hpp"
#include "animgraph/skeleton/skeleton.hpp"

#include <array>
#include <span>
#include <vector>

namespace animgraph {

struct Vec2 { float x{}; float y{}; };
struct Blend1DSample { float threshold{}; LocalPose pose; };
struct Blend2DSample { Vec2 point; LocalPose pose; };
struct Blend2DResult { LocalPose pose; std::array<float, 3> weights{}; };
struct LayerBranch { JointId root; float weight{}; };

[[nodiscard]] Expected<LocalPose, Error> blend_poses(const LocalPose& a,
                                                     const LocalPose& b,
                                                     float weight);
[[nodiscard]] Expected<LocalPose, Error> evaluate_blend_1d(
    std::span<const Blend1DSample> samples, float value);
[[nodiscard]] Expected<Blend2DResult, Error> evaluate_blend_2d(
    std::span<const Blend2DSample, 3> samples, Vec2 value);
[[nodiscard]] Expected<LocalPose, Error> additive_pose(
    const LocalPose& base, const LocalPose& additive, const LocalPose& reference,
    float weight);
[[nodiscard]] Expected<std::vector<float>, Error> make_hierarchy_mask(
    const CompiledSkeleton& skeleton, std::span<const LayerBranch> branches);
[[nodiscard]] Expected<LocalPose, Error> layered_blend(
    const LocalPose& base, const LocalPose& layer, std::span<const float> weights);

}  // namespace animgraph
