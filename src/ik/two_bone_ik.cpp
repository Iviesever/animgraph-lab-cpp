#include "animgraph/ik/two_bone_ik.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace animgraph {
namespace {

constexpr float ik_epsilon = 1.0e-5F;

Expected<Quat, Error> model_to_local_rotation(const CompiledSkeleton& skeleton,
                                              const ModelPose& model, JointId joint,
                                              Quat desired_model) {
  const auto parent = skeleton.joints[joint.value].parent;
  if (!parent) return normalize(desired_model);
  const auto inverse_parent = inverse(model.transforms[parent->value].rotation);
  if (!inverse_parent) return make_unexpected(inverse_parent.error());
  return normalize(multiply(*inverse_parent, desired_model));
}

}  // namespace

Expected<IkResult, Error> solve_two_bone_ik(
    const CompiledSkeleton& skeleton, LocalPose& pose,
    const TwoBoneIkRequest& request) {
  if (request.root.value >= skeleton.joints.size() || request.mid.value >= skeleton.joints.size() ||
      request.end.value >= skeleton.joints.size() || pose.transforms.size() != skeleton.joints.size() ||
      skeleton.joints[request.mid.value].parent != request.root ||
      skeleton.joints[request.end.value].parent != request.mid || !finite(request.target_model) ||
      !finite(request.pole_model) || !std::isfinite(request.weight) || request.weight < 0.0F ||
      request.weight > 1.0F) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "two-bone IK request is invalid"});
  }
  if (request.limit && (!std::isfinite(request.limit->min_bend_radians) ||
                        !std::isfinite(request.limit->max_bend_radians) ||
                        request.limit->min_bend_radians < 0.0F ||
                        request.limit->max_bend_radians > std::numbers::pi_v<float> ||
                        request.limit->min_bend_radians > request.limit->max_bend_radians)) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "IK joint limit is invalid"});
  }
  const auto original_model = local_to_model(skeleton, pose);
  if (!original_model) return make_unexpected(original_model.error());
  const Vec3 root_position = original_model->transforms[request.root.value].translation;
  const Vec3 mid_position = original_model->transforms[request.mid.value].translation;
  const Vec3 end_position = original_model->transforms[request.end.value].translation;
  const float first_length = length(mid_position - root_position);
  const float second_length = length(end_position - mid_position);
  if (first_length <= ik_epsilon || second_length <= ik_epsilon) {
    return IkResult{IkStatus::degenerate, end_position,
                    length(request.target_model - end_position), 0.0F, false};
  }

  const Vec3 target_offset = request.target_model - root_position;
  const float requested_distance = length(target_offset);
  const float minimum_distance = std::abs(first_length - second_length) + ik_epsilon;
  const float maximum_distance = first_length + second_length - ik_epsilon;
  IkStatus status = IkStatus::reached;
  if (requested_distance >= maximum_distance) status = IkStatus::fully_extended;
  else if (requested_distance <= minimum_distance) status = IkStatus::too_close;

  Vec3 direction = requested_distance > ik_epsilon
      ? target_offset / requested_distance
      : normalize(mid_position - root_position).value();
  float solve_distance = std::clamp(requested_distance, minimum_distance, maximum_distance);
  float bend = std::acos(std::clamp(
      (first_length * first_length + second_length * second_length - solve_distance * solve_distance) /
      (2.0F * first_length * second_length), -1.0F, 1.0F));
  if (request.limit) {
    bend = std::clamp(bend, request.limit->min_bend_radians,
                      request.limit->max_bend_radians);
    solve_distance = std::sqrt(std::max(ik_epsilon,
        first_length * first_length + second_length * second_length -
        2.0F * first_length * second_length * std::cos(bend)));
  }
  const Vec3 solved_target = root_position + direction * solve_distance;
  Vec3 pole = request.pole_model - root_position;
  pole = pole - direction * dot(pole, direction);
  bool pole_fallback = false;
  auto pole_normalized = normalize(pole);
  if (!pole_normalized) {
    pole_fallback = true;
    Vec3 candidate = cross(direction, Vec3{0, 0, 1});
    if (length_squared(candidate) <= ik_epsilon) candidate = cross(direction, Vec3{0, 1, 0});
    pole_normalized = normalize(candidate);
  }
  if (!pole_normalized) return make_unexpected(pole_normalized.error());

  const float along = std::clamp(
      (first_length * first_length + solve_distance * solve_distance - second_length * second_length) /
      (2.0F * solve_distance), -first_length, first_length);
  const float height = std::sqrt(std::max(0.0F, first_length * first_length - along * along));
  const Vec3 desired_mid = root_position + direction * along + *pole_normalized * height;

  const auto root_delta = from_to_rotation(mid_position - root_position,
                                           desired_mid - root_position);
  if (!root_delta) return make_unexpected(root_delta.error());
  const Quat desired_root_model = multiply(*root_delta,
      original_model->transforms[request.root.value].rotation);
  const auto solved_root_local = model_to_local_rotation(skeleton, *original_model,
                                                         request.root, desired_root_model);
  if (!solved_root_local) return make_unexpected(solved_root_local.error());
  const Quat original_root = pose.transforms[request.root.value].rotation;
  pose.transforms[request.root.value].rotation = slerp(original_root, *solved_root_local,
                                                       request.weight);

  auto root_adjusted = local_to_model(skeleton, pose);
  if (!root_adjusted) return make_unexpected(root_adjusted.error());
  const Vec3 adjusted_mid = root_adjusted->transforms[request.mid.value].translation;
  const Vec3 adjusted_end = root_adjusted->transforms[request.end.value].translation;
  const auto mid_delta = from_to_rotation(adjusted_end - adjusted_mid,
                                          solved_target - adjusted_mid);
  if (!mid_delta) return make_unexpected(mid_delta.error());
  const Quat desired_mid_model = multiply(*mid_delta,
      root_adjusted->transforms[request.mid.value].rotation);
  const auto solved_mid_local = model_to_local_rotation(skeleton, *root_adjusted,
                                                        request.mid, desired_mid_model);
  if (!solved_mid_local) return make_unexpected(solved_mid_local.error());
  const Quat original_mid = pose.transforms[request.mid.value].rotation;
  pose.transforms[request.mid.value].rotation = slerp(original_mid, *solved_mid_local,
                                                      request.weight);

  const auto final_model = local_to_model(skeleton, pose);
  if (!final_model) return make_unexpected(final_model.error());
  const Vec3 achieved = final_model->transforms[request.end.value].translation;
  return IkResult{status, achieved, length(request.target_model - achieved), bend,
                  pole_fallback};
}

}  // namespace animgraph
