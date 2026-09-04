#include "animgraph/ik/two_bone_ik.hpp"
#include "animgraph/runtime/batch.hpp"
#include "test_support.hpp"

#include <array>
#include <cmath>
#include <stop_token>

namespace {

using namespace animgraph;

CompiledSkeleton limb_skeleton(bool degenerate = false) {
  return compile_skeleton(RawSkeleton{{
      RawJoint{.name = "root", .parent = std::nullopt, .reference_local = Transform::identity(),
               .inverse_bind = Transform::identity(), .semantic = "root"},
      RawJoint{.name = "mid", .parent = 0,
               .reference_local = Transform{Vec3{degenerate ? 0.0F : 1.0F, 0, 0}, Quat::identity(), Vec3{1, 1, 1}},
               .inverse_bind = Transform::identity(), .semantic = "elbow"},
      RawJoint{.name = "end", .parent = 1,
               .reference_local = Transform{Vec3{1, 0, 0}, Quat::identity(), Vec3{1, 1, 1}},
               .inverse_bind = Transform::identity(), .semantic = "hand"},
      RawJoint{.name = "sibling", .parent = 0,
               .reference_local = Transform{Vec3{0, 0, 1}, Quat::identity(), Vec3{1, 1, 1}},
               .inverse_bind = Transform::identity(), .semantic = std::nullopt}}}).value();
}

LocalPose reference_pose(const CompiledSkeleton& skeleton) {
  LocalPose pose;
  for (const auto& joint : skeleton.joints) pose.transforms.push_back(joint.reference_local);
  return pose;
}

ANIMGRAPH_TEST(two_bone_ik_reaches_target_preserves_lengths_and_isolates_chain) {
  const auto skeleton = limb_skeleton();
  auto pose = reference_pose(skeleton);
  const Transform untouched_end = pose.transforms[2];
  const Transform untouched_sibling = pose.transforms[3];
  const auto before = local_to_model(skeleton, pose).value();
  const auto result = solve_two_bone_ik(skeleton, pose,
      TwoBoneIkRequest{JointId{0}, JointId{1}, JointId{2}, Vec3{1, 1, 0}, Vec3{0, 0, 1},
                       1.0F, std::nullopt});
  AG_CHECK(result.has_value());
  AG_CHECK_EQ(result->status, IkStatus::reached);
  const auto after = local_to_model(skeleton, pose).value();
  AG_CHECK(length(after.transforms[2].translation - Vec3{1, 1, 0}) < 1.0e-4F);
  AG_CHECK_NEAR(length(before.transforms[1].translation - before.transforms[0].translation),
                length(after.transforms[1].translation - after.transforms[0].translation), 1.0e-5F);
  AG_CHECK_NEAR(length(before.transforms[2].translation - before.transforms[1].translation),
                length(after.transforms[2].translation - after.transforms[1].translation), 1.0e-5F);
  AG_CHECK_EQ(pose.transforms[2], untouched_end);
  AG_CHECK_EQ(pose.transforms[3], untouched_sibling);
}

ANIMGRAPH_TEST(two_bone_ik_handles_extended_too_close_pole_weight_and_limits) {
  const auto skeleton = limb_skeleton();
  for (const auto& [target, expected] : std::array{
           std::pair{Vec3{10, 0, 0}, IkStatus::fully_extended},
           std::pair{Vec3{0, 0, 0}, IkStatus::too_close}}) {
    auto pose = reference_pose(skeleton);
    const auto result = solve_two_bone_ik(skeleton, pose,
        TwoBoneIkRequest{JointId{0}, JointId{1}, JointId{2}, target, Vec3{1, 0, 0},
                         1.0F, JointLimit{0.1F, 2.8F}});
    AG_CHECK(result.has_value());
    AG_CHECK_EQ(result->status, expected);
    for (const auto& transform : pose.transforms) AG_CHECK(finite(transform));
    AG_CHECK(result->bend_radians >= 0.1F && result->bend_radians <= 2.8F);
  }
  auto unchanged = reference_pose(skeleton);
  const auto original = unchanged.transforms;
  AG_CHECK(solve_two_bone_ik(skeleton, unchanged,
      TwoBoneIkRequest{JointId{0}, JointId{1}, JointId{2}, Vec3{1, 1, 0}, Vec3{0, 0, 1},
                       0.0F, std::nullopt}).has_value());
  AG_CHECK_EQ(unchanged.transforms, original);
}

ANIMGRAPH_TEST(two_bone_ik_degenerate_limb_returns_finite_typed_status) {
  const auto skeleton = limb_skeleton(true);
  auto pose = reference_pose(skeleton);
  const auto result = solve_two_bone_ik(skeleton, pose,
      TwoBoneIkRequest{JointId{0}, JointId{1}, JointId{2}, Vec3{1, 1, 0}, Vec3{0, 0, 1},
                       1.0F, std::nullopt});
  AG_CHECK(result.has_value());
  AG_CHECK_EQ(result->status, IkStatus::degenerate);
  for (const auto& transform : pose.transforms) AG_CHECK(finite(transform));
}

ANIMGRAPH_TEST(compiled_two_bone_ik_node_uses_the_shared_solver) {
  const auto skeleton = limb_skeleton();
  GraphBuilder builder;
  const auto reference = builder.add_node(NodeType::reference_pose, "reference");
  const auto ik = builder.add_node(NodeType::two_bone_ik, "ik");
  TwoBoneIkNodeConfig ik_config;
  ik_config.root = JointId{0}; ik_config.mid = JointId{1}; ik_config.end = JointId{2};
  ik_config.pole = Vec3{0,0,1};
  ik_config.parameters = {"p0_target_x","p1_target_y","p2_target_z","p3_weight"};
  builder.configure_two_bone_ik(ik, std::move(ik_config)).value();
  builder.connect(PosePin{reference, 0}, PosePin{ik, 0}).value();
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{ik, 0}, PosePin{output, 0}).value();
  builder.set_output(output);
  builder.add_parameter(GraphParameter{"p0_target_x", 1.0F, false});
  builder.add_parameter(GraphParameter{"p1_target_y", 1.0F, false});
  builder.add_parameter(GraphParameter{"p2_target_z", 0.0F, false});
  builder.add_parameter(GraphParameter{"p3_weight", 1.0F, false});
  const auto graph = compile_graph(builder.build()).value();
  auto instance = make_graph_instance(graph, skeleton.joints.size()).value();
  const std::array parameters{1.0F, 1.0F, 0.0F, 1.0F};
  const EvaluationContext context{skeleton, {}, AnimTime{0}, parameters, 1, 1};
  const auto result = evaluate(context, graph, instance);
  AG_CHECK(result.has_value());
  const auto model = local_to_model(skeleton, result->pose).value();
  AG_CHECK(length(model.transforms[2].translation - Vec3{1, 1, 0}) < 1.0e-4F);
}

CompiledGraph batch_graph() {
  GraphBuilder builder;
  const auto reference = builder.add_node(NodeType::reference_pose, "reference");
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{reference, 0}, PosePin{output, 0}).value();
  builder.set_output(output);
  return compile_graph(builder.build()).value();
}

ANIMGRAPH_TEST(batch_parallel_matches_serial_and_sorts_character_ids) {
  const auto skeleton = limb_skeleton();
  const auto graph = batch_graph();
  std::array<GraphInstance, 4> serial_instances{
      make_graph_instance(graph, 4).value(), make_graph_instance(graph, 4).value(),
      make_graph_instance(graph, 4).value(), make_graph_instance(graph, 4).value()};
  auto parallel_instances = serial_instances;
  std::array<EvaluationContext, 4> contexts{
      EvaluationContext{skeleton, {}, AnimTime{0}, {}, 1, 1},
      EvaluationContext{skeleton, {}, AnimTime{0}, {}, 1, 1},
      EvaluationContext{skeleton, {}, AnimTime{0}, {}, 1, 1},
      EvaluationContext{skeleton, {}, AnimTime{0}, {}, 1, 1}};
  std::array serial_jobs{
      EvaluationJob{CharacterId{40}, &contexts[0], &graph, &serial_instances[0]},
      EvaluationJob{CharacterId{10}, &contexts[1], &graph, &serial_instances[1]},
      EvaluationJob{CharacterId{30}, &contexts[2], &graph, &serial_instances[2]},
      EvaluationJob{CharacterId{20}, &contexts[3], &graph, &serial_instances[3]}};
  std::array parallel_jobs{
      EvaluationJob{CharacterId{40}, &contexts[0], &graph, &parallel_instances[0]},
      EvaluationJob{CharacterId{10}, &contexts[1], &graph, &parallel_instances[1]},
      EvaluationJob{CharacterId{30}, &contexts[2], &graph, &parallel_instances[2]},
      EvaluationJob{CharacterId{20}, &contexts[3], &graph, &parallel_instances[3]}};
  const auto serial = evaluate_serial(serial_jobs);
  for (const std::size_t workers : {1U, 2U, 4U}) {
    parallel_instances = serial_instances;
    for (std::size_t index = 0; index < parallel_jobs.size(); ++index)
      parallel_jobs[index].instance = &parallel_instances[index];
    const auto parallel = evaluate_parallel(parallel_jobs, workers);
    AG_CHECK_EQ(parallel.size(), serial.size());
    for (std::size_t index = 0; index < serial.size(); ++index) {
      AG_CHECK_EQ(parallel[index].character.value, (index + 1) * 10U);
      AG_CHECK(!parallel[index].error.has_value());
      AG_CHECK_EQ(parallel[index].result.pose.transforms, serial[index].result.pose.transforms);
      AG_CHECK_EQ(parallel[index].result.events.size(), serial[index].result.events.size());
    }
  }
}

ANIMGRAPH_TEST(batch_parallel_honors_pre_requested_cancellation_and_joins) {
  const auto skeleton = limb_skeleton();
  const auto graph = batch_graph();
  auto instance = make_graph_instance(graph, 4).value();
  const EvaluationContext context{skeleton, {}, AnimTime{0}, {}, 1, 1};
  std::array jobs{EvaluationJob{CharacterId{1}, &context, &graph, &instance}};
  std::stop_source stop;
  stop.request_stop();
  const auto results = evaluate_parallel(jobs, 2, stop.get_token());
  AG_CHECK_EQ(results.size(), 1U);
  AG_CHECK(results[0].error.has_value());
  AG_CHECK_EQ(results[0].error->code, ErrorCode::cancelled);
}

}  // namespace
