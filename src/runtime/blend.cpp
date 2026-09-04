#include "animgraph/runtime/blend.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace animgraph {
namespace {

bool valid_weight(float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; }

Transform blend_transform(const Transform& a, const Transform& b, float weight) {
  return {a.translation + (b.translation - a.translation) * weight,
          slerp(a.rotation, b.rotation, weight),
          a.scale + (b.scale - a.scale) * weight};
}

}  // namespace

Expected<LocalPose, Error> blend_poses(const LocalPose& a, const LocalPose& b,
                                      float weight) {
  if (a.transforms.size() != b.transforms.size() || !valid_weight(weight)) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "pose blend dimensions or weight are invalid"});
  }
  LocalPose result;
  result.transforms.reserve(a.transforms.size());
  for (std::size_t index = 0; index < a.transforms.size(); ++index) {
    if (!finite(a.transforms[index]) || !finite(b.transforms[index])) {
      return make_unexpected(Error{ErrorCode::non_finite, "pose blend contains invalid transform"});
    }
    result.transforms.push_back(blend_transform(a.transforms[index], b.transforms[index], weight));
  }
  return result;
}

Expected<LocalPose, Error> evaluate_blend_1d(std::span<const Blend1DSample> samples,
                                            float value) {
  if (samples.empty() || !std::isfinite(value)) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "Blend1D input is invalid"});
  }
  std::vector<Blend1DSample> ordered(samples.begin(), samples.end());
  std::ranges::stable_sort(ordered, {}, &Blend1DSample::threshold);
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    if (!std::isfinite(ordered[index].threshold) ||
        (index > 0 && ordered[index - 1].threshold == ordered[index].threshold)) {
      return make_unexpected(Error{ErrorCode::invalid_argument, "Blend1D threshold is invalid or duplicated"});
    }
  }
  if (value <= ordered.front().threshold) return ordered.front().pose;
  if (value >= ordered.back().threshold) return ordered.back().pose;
  const auto upper = std::upper_bound(ordered.begin(), ordered.end(), value,
      [](float candidate, const Blend1DSample& sample) { return candidate < sample.threshold; });
  const auto lower = upper - 1;
  const float weight = (value - lower->threshold) / (upper->threshold - lower->threshold);
  return blend_poses(lower->pose, upper->pose, weight);
}

Expected<Blend2DResult, Error> evaluate_blend_2d(
    std::span<const Blend2DSample, 3> samples, Vec2 value) {
  if (!std::isfinite(value.x) || !std::isfinite(value.y)) {
    return make_unexpected(Error{ErrorCode::non_finite, "Blend2D coordinate is not finite"});
  }
  const auto& a = samples[0].point; const auto& b = samples[1].point; const auto& c = samples[2].point;
  const float determinant = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
  if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8F) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "Blend2D triangle is degenerate"});
  }
  std::array<float, 3> weights{
      ((b.y - c.y) * (value.x - c.x) + (c.x - b.x) * (value.y - c.y)) / determinant,
      ((c.y - a.y) * (value.x - c.x) + (a.x - c.x) * (value.y - c.y)) / determinant,
      0.0F};
  weights[2] = 1.0F - weights[0] - weights[1];
  for (auto& weight : weights) weight = std::max(0.0F, weight);
  const float total = weights[0] + weights[1] + weights[2];
  if (total <= 1.0e-8F) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "Blend2D projection has no valid weight"});
  }
  for (auto& weight : weights) weight /= total;
  const std::size_t count = samples[0].pose.transforms.size();
  if (samples[1].pose.transforms.size() != count || samples[2].pose.transforms.size() != count) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "Blend2D pose dimensions differ"});
  }
  Blend2DResult result;
  result.weights = weights;
  result.pose.transforms.reserve(count);
  for (std::size_t joint = 0; joint < count; ++joint) {
    const auto& first = samples[0].pose.transforms[joint];
    const auto& second = samples[1].pose.transforms[joint];
    const auto& third = samples[2].pose.transforms[joint];
    Quat q1 = second.rotation, q2 = third.rotation;
    if (dot(first.rotation, q1) < 0) q1 = {-q1.x, -q1.y, -q1.z, -q1.w};
    if (dot(first.rotation, q2) < 0) q2 = {-q2.x, -q2.y, -q2.z, -q2.w};
    const auto rotation = normalize(Quat{
        first.rotation.x * weights[0] + q1.x * weights[1] + q2.x * weights[2],
        first.rotation.y * weights[0] + q1.y * weights[1] + q2.y * weights[2],
        first.rotation.z * weights[0] + q1.z * weights[1] + q2.z * weights[2],
        first.rotation.w * weights[0] + q1.w * weights[1] + q2.w * weights[2]});
    if (!rotation) return make_unexpected(rotation.error());
    result.pose.transforms.push_back({
        first.translation * weights[0] + second.translation * weights[1] + third.translation * weights[2],
        *rotation,
        first.scale * weights[0] + second.scale * weights[1] + third.scale * weights[2]});
  }
  return result;
}

Expected<LocalPose, Error> additive_pose(const LocalPose& base, const LocalPose& additive,
                                        const LocalPose& reference, float weight) {
  if (base.transforms.size() != additive.transforms.size() ||
      base.transforms.size() != reference.transforms.size() || !valid_weight(weight)) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "additive pose dimensions or weight are invalid"});
  }
  LocalPose result;
  result.transforms.reserve(base.transforms.size());
  for (std::size_t index = 0; index < base.transforms.size(); ++index) {
    const auto inverse_reference = inverse(reference.transforms[index].rotation);
    if (!inverse_reference) return make_unexpected(inverse_reference.error());
    const Quat delta = multiply(*inverse_reference, additive.transforms[index].rotation);
    const Quat weighted = slerp(Quat::identity(), delta, weight);
    result.transforms.push_back({
        base.transforms[index].translation +
            (additive.transforms[index].translation - reference.transforms[index].translation) * weight,
        normalize(multiply(base.transforms[index].rotation, weighted)).value(),
        base.transforms[index].scale +
            (additive.transforms[index].scale - reference.transforms[index].scale) * weight});
  }
  return result;
}

Expected<std::vector<float>, Error> make_hierarchy_mask(
    const CompiledSkeleton& skeleton, std::span<const LayerBranch> branches) {
  std::vector<float> result(skeleton.joints.size(), 0.0F);
  std::vector<LayerBranch> ordered(branches.begin(), branches.end());
  std::ranges::stable_sort(ordered, {}, [](const LayerBranch& branch) { return branch.root.value; });
  for (const auto& branch : ordered) {
    if (branch.root.value >= skeleton.joints.size() || !valid_weight(branch.weight)) {
      return make_unexpected(Error{ErrorCode::invalid_argument, "layer branch is invalid"});
    }
    for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint) {
      std::optional<JointId> cursor{JointId{static_cast<std::uint32_t>(joint)}};
      while (cursor) {
        if (*cursor == branch.root) { result[joint] = branch.weight; break; }
        cursor = skeleton.joints[cursor->value].parent;
      }
    }
  }
  return result;
}

Expected<LocalPose, Error> layered_blend(const LocalPose& base, const LocalPose& layer,
                                        std::span<const float> weights) {
  if (base.transforms.size() != layer.transforms.size() || base.transforms.size() != weights.size()) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "layered blend dimensions differ"});
  }
  LocalPose result;
  result.transforms.reserve(base.transforms.size());
  for (std::size_t index = 0; index < base.transforms.size(); ++index) {
    if (!valid_weight(weights[index]))
      return make_unexpected(Error{ErrorCode::invalid_argument, "layer weight is invalid"});
    result.transforms.push_back(blend_transform(base.transforms[index], layer.transforms[index], weights[index]));
  }
  return result;
}

}  // namespace animgraph
