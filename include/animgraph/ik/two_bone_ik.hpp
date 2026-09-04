#pragma once

#include "animgraph/core/expected.hpp"
#include "animgraph/skeleton/skeleton.hpp"

#include <optional>

namespace animgraph {

enum class IkStatus { reached, fully_extended, too_close, degenerate };
struct JointLimit { float min_bend_radians{}; float max_bend_radians{}; };
struct TwoBoneIkRequest {
  JointId root;
  JointId mid;
  JointId end;
  Vec3 target_model;
  Vec3 pole_model;
  float weight{1.0F};
  std::optional<JointLimit> limit;
};
struct IkResult {
  IkStatus status{IkStatus::degenerate};
  Vec3 achieved_model;
  float target_error{};
  float bend_radians{};
  bool used_pole_fallback{};
};

[[nodiscard]] Expected<IkResult, Error> solve_two_bone_ik(
    const CompiledSkeleton& skeleton, LocalPose& pose,
    const TwoBoneIkRequest& request);

}  // namespace animgraph
