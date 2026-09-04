#include "animgraph/skeleton/skeleton.hpp"

#include <cmath>
#include <limits>

namespace animgraph {
namespace {

bool valid_transform(const Transform& value) {
  return finite(value) && std::abs(value.scale.x) > 1.0e-8F &&
         std::abs(value.scale.y) > 1.0e-8F && std::abs(value.scale.z) > 1.0e-8F &&
         normalize(value.rotation).has_value();
}

Expected<void, Error> validate_raw(const RawSkeleton& raw) {
  if (raw.joints.empty() || raw.joints.size() > max_joints) {
    return make_unexpected(Error{ErrorCode::bounds, "skeleton joint count is outside [1,256]"});
  }
  std::size_t roots = 0;
  for (std::size_t index = 0; index < raw.joints.size(); ++index) {
    const auto& current = raw.joints[index];
    if (current.name.empty()) {
      return make_unexpected(Error{ErrorCode::invalid_argument, "joint name is empty"});
    }
    if (!valid_transform(current.reference_local) || !valid_transform(current.inverse_bind)) {
      return make_unexpected(Error{ErrorCode::non_finite, "joint transform is invalid"});
    }
    if (!current.parent) {
      ++roots;
    } else if (*current.parent >= raw.joints.size() || *current.parent == index) {
      return make_unexpected(Error{ErrorCode::hierarchy, "joint parent is invalid"});
    }
  }
  if (roots != 1) {
    return make_unexpected(Error{ErrorCode::hierarchy, "skeleton must have exactly one root"});
  }

  for (std::size_t start = 0; start < raw.joints.size(); ++start) {
    std::vector<bool> visited(raw.joints.size(), false);
    std::optional<std::uint32_t> cursor{static_cast<std::uint32_t>(start)};
    while (cursor) {
      if (visited[*cursor]) {
        return make_unexpected(Error{ErrorCode::hierarchy, "skeleton contains a cycle"});
      }
      visited[*cursor] = true;
      cursor = raw.joints[*cursor].parent;
    }
  }
  return {};
}

}  // namespace

Expected<std::uint32_t, Error> SkeletonBuilder::add_joint(RawJoint joint_value) {
  if (raw_.joints.size() >= max_joints) {
    return make_unexpected(Error{ErrorCode::bounds, "skeleton exceeds joint limit"});
  }
  const auto index = static_cast<std::uint32_t>(raw_.joints.size());
  raw_.joints.push_back(std::move(joint_value));
  return index;
}
Expected<CompiledSkeleton, Error> SkeletonBuilder::build() const {
  return compile_skeleton(raw_);
}
Expected<void, Error> SkeletonValidator::validate(const RawSkeleton& raw) {
  return validate_raw(raw);
}

Expected<CompiledSkeleton, Error> compile_skeleton(const RawSkeleton& raw) {
  const auto validation = validate_raw(raw);
  if (!validation) return make_unexpected(validation.error());

  CompiledSkeleton compiled;
  compiled.joints.reserve(raw.joints.size());
  compiled.original_to_compiled.resize(raw.joints.size());
  compiled.compiled_to_original.reserve(raw.joints.size());
  std::vector<bool> emitted(raw.joints.size(), false);

  while (compiled.joints.size() < raw.joints.size()) {
    bool progressed = false;
    for (std::size_t original = 0; original < raw.joints.size(); ++original) {
      if (emitted[original]) continue;
      const auto& source = raw.joints[original];
      if (source.parent && !emitted[*source.parent]) continue;
      const auto compiled_index = static_cast<std::uint32_t>(compiled.joints.size());
      compiled.original_to_compiled[original] = JointId{compiled_index};
      compiled.compiled_to_original.push_back(static_cast<std::uint32_t>(original));
      compiled.joints.push_back(Joint{
          .id = JointId{compiled_index},
          .name = source.name,
          .parent = source.parent ? std::optional<JointId>{compiled.original_to_compiled[*source.parent]}
                                  : std::nullopt,
          .reference_local = source.reference_local,
          .inverse_bind = source.inverse_bind,
          .semantic = source.semantic,
          .original_index = static_cast<std::uint32_t>(original)});
      emitted[original] = true;
      progressed = true;
    }
    if (!progressed) {
      return make_unexpected(Error{ErrorCode::hierarchy, "skeleton ordering could not be resolved"});
    }
  }
  return compiled;
}

Expected<ModelPose, Error> local_to_model(const CompiledSkeleton& skeleton,
                                          const LocalPose& local) {
  if (local.transforms.size() != skeleton.joints.size()) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "local pose joint count mismatch"});
  }
  ModelPose model;
  model.transforms.resize(skeleton.joints.size());
  for (std::size_t index = 0; index < skeleton.joints.size(); ++index) {
    if (!finite(local.transforms[index])) {
      return make_unexpected(Error{ErrorCode::non_finite, "local pose contains invalid transform"});
    }
    const auto parent = skeleton.joints[index].parent;
    model.transforms[index] = parent ? compose(model.transforms[parent->value], local.transforms[index])
                                     : local.transforms[index];
  }
  return model;
}

Expected<SkinMatrixPalette, Error> model_to_skin(const CompiledSkeleton& skeleton,
                                                const ModelPose& model) {
  if (model.transforms.size() != skeleton.joints.size()) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "model pose joint count mismatch"});
  }
  SkinMatrixPalette palette;
  palette.matrices.reserve(model.transforms.size());
  for (std::size_t index = 0; index < model.transforms.size(); ++index) {
    if (!finite(model.transforms[index])) {
      return make_unexpected(Error{ErrorCode::non_finite, "model pose contains invalid transform"});
    }
    palette.matrices.push_back(to_matrix(compose(model.transforms[index],
                                                 skeleton.joints[index].inverse_bind)));
  }
  return palette;
}

}  // namespace animgraph
