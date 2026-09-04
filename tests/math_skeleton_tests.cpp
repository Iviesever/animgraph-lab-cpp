#include "animgraph/math/math.hpp"
#include "animgraph/skeleton/skeleton.hpp"
#include "test_support.hpp"

#include <cmath>
#include <limits>
#include <numbers>

namespace {

using namespace animgraph;

bool near(Vec3 a, Vec3 b, float tolerance = 1.0e-4F) {
  return length(a - b) <= tolerance;
}

RawJoint joint(std::string name, std::optional<std::uint32_t> parent,
               Vec3 translation = {}) {
  return RawJoint{.name = std::move(name),
                  .parent = parent,
                  .reference_local = Transform{translation, Quat::identity(), Vec3{1, 1, 1}},
                  .inverse_bind = Transform::identity(),
                  .semantic = std::nullopt};
}

ANIMGRAPH_TEST(vec_and_quaternion_reject_invalid_normalization) {
  AG_CHECK(!normalize(Vec3{}));
  AG_CHECK(!normalize(Quat{}));
  AG_CHECK(!normalize(Vec3{std::numeric_limits<float>::infinity(), 0, 0}));
  AG_CHECK(!normalize(Quat{0, std::numeric_limits<float>::quiet_NaN(), 0, 1}));
}

ANIMGRAPH_TEST(quaternion_rotation_interpolation_and_antipodes_are_stable) {
  const Quat identity = Quat::identity();
  const auto half_turn = from_axis_angle(Vec3{0, 0, 1}, std::numbers::pi_v<float>);
  AG_CHECK(half_turn.has_value());
  AG_CHECK(near(rotate(*half_turn, Vec3{1, 0, 0}), Vec3{-1, 0, 0}));
  AG_CHECK_EQ(slerp(identity, *half_turn, 0.0F), identity);
  AG_CHECK_EQ(slerp(identity, *half_turn, 1.0F), *half_turn);
  const Quat negative{-identity.x, -identity.y, -identity.z, -identity.w};
  AG_CHECK(near(rotate(nlerp(identity, negative, 0.5F), Vec3{1, 2, 3}), Vec3{1, 2, 3}));
}

ANIMGRAPH_TEST(quaternion_multiply_inverse_and_from_to_are_consistent) {
  const auto turn = from_axis_angle(Vec3{0, 1, 0}, 0.75F);
  AG_CHECK(turn.has_value());
  const auto inverse_turn = inverse(*turn);
  AG_CHECK(inverse_turn.has_value());
  AG_CHECK(near(rotate(multiply(*turn, *inverse_turn), Vec3{2, 3, 4}), Vec3{2, 3, 4}));
  const auto between = from_to_rotation(Vec3{1, 0, 0}, Vec3{0, 1, 0});
  AG_CHECK(between.has_value());
  AG_CHECK(near(rotate(*between, Vec3{1, 0, 0}), Vec3{0, 1, 0}));
}

ANIMGRAPH_TEST(transform_compose_and_inverse_restore_points) {
  const auto rotation = from_axis_angle(Vec3{0, 0, 1}, 0.5F);
  AG_CHECK(rotation.has_value());
  const Transform parent{Vec3{2, 3, 4}, *rotation, Vec3{2, 2, 2}};
  const Transform child{Vec3{1, 0, 0}, Quat::identity(), Vec3{1, 1, 1}};
  const auto combined = compose(parent, child);
  AG_CHECK(combined.has_value());
  const auto restored = inverse(*combined);
  AG_CHECK(restored.has_value());
  const Vec3 point{0.25F, -0.5F, 1.0F};
  AG_CHECK(near(transform_point(*restored, transform_point(*combined, point)), point));
}

ANIMGRAPH_TEST(skeleton_rejects_cycles_multiple_roots_and_non_finite_bind_data) {
  AG_CHECK(!compile_skeleton(RawSkeleton{{joint("a", 1), joint("b", 0)}}));
  AG_CHECK(!compile_skeleton(RawSkeleton{{joint("a", std::nullopt), joint("b", std::nullopt)}}));
  auto invalid = joint("root", std::nullopt);
  invalid.reference_local.translation.x = std::numeric_limits<float>::quiet_NaN();
  AG_CHECK(!compile_skeleton(RawSkeleton{{invalid}}));
}

ANIMGRAPH_TEST(skeleton_compilation_is_stable_parent_before_child) {
  RawSkeleton raw{{joint("hand", 2, Vec3{1, 0, 0}),
                   joint("root", std::nullopt, Vec3{10, 0, 0}),
                   joint("arm", 1, Vec3{2, 0, 0})}};
  const auto compiled = compile_skeleton(raw);
  AG_CHECK(compiled.has_value());
  AG_CHECK_EQ(compiled->joints[0].name, std::string{"root"});
  AG_CHECK_EQ(compiled->joints[1].name, std::string{"arm"});
  AG_CHECK_EQ(compiled->joints[2].name, std::string{"hand"});
  AG_CHECK_EQ(compiled->original_to_compiled[0].value, 2U);
  AG_CHECK_EQ(compiled->compiled_to_original[2], 0U);
}

ANIMGRAPH_TEST(local_model_and_skin_pose_match_reference_oracle) {
  const auto skeleton = compile_skeleton(RawSkeleton{{
      joint("root", std::nullopt, Vec3{1, 0, 0}),
      joint("child", 0, Vec3{2, 0, 0})}});
  AG_CHECK(skeleton.has_value());
  LocalPose local{{skeleton->joints[0].reference_local, skeleton->joints[1].reference_local}};
  const auto model = local_to_model(*skeleton, local);
  AG_CHECK(model.has_value());
  AG_CHECK(near(model->transforms[0].translation, Vec3{1, 0, 0}));
  AG_CHECK(near(model->transforms[1].translation, Vec3{3, 0, 0}));
  const auto skin = model_to_skin(*skeleton, *model);
  AG_CHECK(skin.has_value());
  AG_CHECK_NEAR(skin->matrices[1][12], 3.0F, 1.0e-5F);
}

ANIMGRAPH_TEST(skeleton_joint_limits_cover_1_2_64_and_256) {
  for (const std::size_t count : {1U, 2U, 64U, 256U}) {
    RawSkeleton raw;
    raw.joints.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      raw.joints.push_back(joint("joint_" + std::to_string(index),
                                 index == 0 ? std::nullopt
                                            : std::optional<std::uint32_t>{static_cast<std::uint32_t>(index - 1)}));
    }
    AG_CHECK(compile_skeleton(raw).has_value());
  }
  RawSkeleton too_many;
  for (std::uint32_t index = 0; index < 257; ++index) {
    too_many.joints.push_back(joint("j" + std::to_string(index),
                                    index == 0 ? std::nullopt
                                               : std::optional<std::uint32_t>{index - 1}));
  }
  AG_CHECK(!compile_skeleton(too_many));
}

}  // namespace
