#pragma once

#include "animgraph/core/types.hpp"
#include "animgraph/math/math.hpp"

#include <optional>
#include <string>
#include <vector>

namespace animgraph {

struct RawJoint {
  std::string name;
  std::optional<std::uint32_t> parent;
  Transform reference_local{Transform::identity()};
  Transform inverse_bind{Transform::identity()};
  std::optional<std::string> semantic;
};

struct RawSkeleton {
  std::vector<RawJoint> joints;
};

struct Joint {
  JointId id;
  std::string name;
  std::optional<JointId> parent;
  Transform reference_local;
  Transform inverse_bind;
  std::optional<std::string> semantic;
  std::uint32_t original_index{};
};

struct CompiledSkeleton {
  std::vector<Joint> joints;
  std::vector<JointId> original_to_compiled;
  std::vector<std::uint32_t> compiled_to_original;
};

using Skeleton = CompiledSkeleton;

class SkeletonBuilder {
 public:
  [[nodiscard]] Expected<std::uint32_t, Error> add_joint(RawJoint joint);
  [[nodiscard]] Expected<CompiledSkeleton, Error> build() const;

 private:
  RawSkeleton raw_;
};

class SkeletonValidator {
 public:
  [[nodiscard]] static Expected<void, Error> validate(const RawSkeleton& raw);
};

struct LocalPose { std::vector<Transform> transforms; };
struct ModelPose { std::vector<Transform> transforms; };
struct SkinMatrixPalette { std::vector<Matrix4> matrices; };

[[nodiscard]] Expected<CompiledSkeleton, Error> compile_skeleton(const RawSkeleton& raw);
[[nodiscard]] Expected<ModelPose, Error> local_to_model(const CompiledSkeleton& skeleton,
                                                         const LocalPose& local);
[[nodiscard]] Expected<SkinMatrixPalette, Error> model_to_skin(const CompiledSkeleton& skeleton,
                                                               const ModelPose& model);

}  // namespace animgraph
