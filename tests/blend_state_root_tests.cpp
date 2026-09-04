#include "animgraph/runtime/blend.hpp"
#include "animgraph/runtime/root_motion.hpp"
#include "animgraph/runtime/runtime.hpp"
#include "animgraph/runtime/state_machine.hpp"
#include "test_support.hpp"

#include <array>
#include <numbers>

namespace {

using namespace animgraph;

LocalPose pose_at(float x, std::size_t joints = 1) {
  LocalPose pose;
  pose.transforms.resize(joints, Transform::identity());
  pose.transforms[0].translation.x = x;
  return pose;
}

CompiledSkeleton chain_skeleton() {
  return compile_skeleton(RawSkeleton{{
      RawJoint{.name = "root", .parent = std::nullopt, .reference_local = Transform::identity(),
               .inverse_bind = Transform::identity(), .semantic = "root"},
      RawJoint{.name = "spine", .parent = 0, .reference_local = Transform::identity(),
               .inverse_bind = Transform::identity(), .semantic = "spine"},
      RawJoint{.name = "hand", .parent = 1, .reference_local = Transform::identity(),
               .inverse_bind = Transform::identity(), .semantic = "hand"}}}).value();
}

AnimationClip constant_clip(std::string name, float x) {
  AnimationClip clip;
  clip.name = std::move(name);
  clip.duration = AnimTime{48'000};
  clip.mode = ClipPlaybackMode::loop;
  JointTrack track;
  track.joint = JointId{0};
  track.translations = {{AnimTime{0}, Vec3{x, 0, 0}}};
  clip.tracks.push_back(std::move(track));
  return clip;
}

ANIMGRAPH_TEST(blend1d_sorts_clamps_hits_exact_samples_and_rejects_duplicates) {
  std::array samples{Blend1DSample{1.0F, pose_at(10)}, Blend1DSample{0.0F, pose_at(0)}};
  AG_CHECK_NEAR(evaluate_blend_1d(samples, 0.5F)->transforms[0].translation.x, 5.0F, 1.0e-5F);
  AG_CHECK_NEAR(evaluate_blend_1d(samples, -1.0F)->transforms[0].translation.x, 0.0F, 1.0e-5F);
  AG_CHECK_NEAR(evaluate_blend_1d(samples, 1.0F)->transforms[0].translation.x, 10.0F, 1.0e-5F);
  samples[1].threshold = 1.0F;
  AG_CHECK(!evaluate_blend_1d(samples, 1.0F));
}

ANIMGRAPH_TEST(blend2d_uses_nonnegative_barycentric_weights_and_rejects_degenerate_triangles) {
  const std::array samples{Blend2DSample{Vec2{0, 0}, pose_at(0)},
                           Blend2DSample{Vec2{1, 0}, pose_at(10)},
                           Blend2DSample{Vec2{0, 1}, pose_at(20)}};
  const auto result = evaluate_blend_2d(samples, Vec2{0.25F, 0.25F});
  AG_CHECK(result.has_value());
  AG_CHECK_NEAR(result->weights[0] + result->weights[1] + result->weights[2], 1.0F, 1.0e-5F);
  for (const auto weight : result->weights) AG_CHECK(weight >= 0.0F);
  AG_CHECK_NEAR(result->pose.transforms[0].translation.x, 7.5F, 1.0e-5F);
  const std::array degenerate{Blend2DSample{Vec2{0, 0}, pose_at(0)},
                              Blend2DSample{Vec2{1, 0}, pose_at(10)},
                              Blend2DSample{Vec2{2, 0}, pose_at(20)}};
  AG_CHECK(!evaluate_blend_2d(degenerate, Vec2{0.5F, 0}));
}

ANIMGRAPH_TEST(additive_rotation_and_layer_hierarchy_masks_are_correct) {
  LocalPose base = pose_at(1, 3);
  LocalPose reference = pose_at(0, 3);
  LocalPose additive = pose_at(3, 3);
  additive.transforms[0].rotation = *from_axis_angle(Vec3{0, 0, 1}, std::numbers::pi_v<float> / 2.0F);
  const auto added = additive_pose(base, additive, reference, 0.5F);
  AG_CHECK(added.has_value());
  AG_CHECK_NEAR(added->transforms[0].translation.x, 2.5F, 1.0e-5F);
  const Vec3 rotated = rotate(added->transforms[0].rotation, Vec3{1, 0, 0});
  AG_CHECK_NEAR(rotated.x, 0.7071067F, 1.0e-4F);
  AG_CHECK_NEAR(rotated.y, 0.7071067F, 1.0e-4F);

  const auto mask = make_hierarchy_mask(chain_skeleton(),
                                        std::array{LayerBranch{JointId{1}, 0.75F}});
  AG_CHECK(mask.has_value());
  AG_CHECK_NEAR((*mask)[0], 0.0F, 1.0e-6F);
  AG_CHECK_NEAR((*mask)[1], 0.75F, 1.0e-6F);
  AG_CHECK_NEAR((*mask)[2], 0.75F, 1.0e-6F);
  const auto layered = layered_blend(base, pose_at(9, 3), *mask);
  AG_CHECK(layered.has_value());
  AG_CHECK_NEAR(layered->transforms[0].translation.x, 1.0F, 1.0e-5F);
}

ANIMGRAPH_TEST(state_machine_selects_stable_priority_and_emits_events_once) {
  StateMachineDefinition definition;
  definition.states = {{StateId{0}, "Idle", AnimTime{48'000}},
                       {StateId{1}, "Run", AnimTime{48'000}},
                       {StateId{2}, "Jump", AnimTime{48'000}}};
  definition.entry = StateId{0};
  definition.transitions = {
      TransitionDefinition{StateId{0}, StateId{1}, ParameterCondition{0, CompareOp::greater, 0.5F},
                           1, AnimTime{12'000}, std::nullopt, std::nullopt, true},
      TransitionDefinition{StateId{0}, StateId{2}, ParameterCondition{0, CompareOp::greater, 0.5F},
                           2, AnimTime{0}, std::nullopt, std::nullopt, true}};
  auto instance = make_state_machine_instance(definition).value();
  const std::array parameters{1.0F};
  const auto first = update_state_machine(definition, parameters, AnimTime{1}, instance);
  AG_CHECK(first.has_value());
  AG_CHECK_EQ(first->target.value, 2U);
  AG_CHECK_EQ(first->events.size(), 2U);
  AG_CHECK_EQ(first->events[0], std::string{"exit:Idle"});
  AG_CHECK_EQ(first->events[1], std::string{"enter:Jump"});
  const auto second = update_state_machine(definition, parameters, AnimTime{1}, instance);
  AG_CHECK(second.has_value());
  AG_CHECK(second->events.empty());
}

ANIMGRAPH_TEST(state_machine_honors_exit_time_and_marker_synchronization) {
  StateMachineDefinition definition;
  definition.states = {{StateId{0}, "Idle", AnimTime{100}}, {StateId{1}, "Run", AnimTime{200}}};
  definition.entry = StateId{0};
  definition.transitions = {TransitionDefinition{
      StateId{0}, StateId{1}, ParameterCondition{0, CompareOp::greater, 0.0F}, 1,
      AnimTime{20}, 0.5F, std::string{"plant"}, false}};
  auto instance = make_state_machine_instance(definition).value();
  const std::array parameters{1.0F};
  AG_CHECK_EQ(update_state_machine(definition, parameters, AnimTime{40}, instance)->target.value, 0U);
  AG_CHECK_EQ(update_state_machine(definition, parameters, AnimTime{20}, instance)->target.value, 1U);

  auto source = constant_clip("source", 0);
  auto target = constant_clip("target", 0);
  source.markers = {{AnimTime{12'000}, "plant"}};
  target.markers = {{AnimTime{24'000}, "plant"}};
  const auto synchronized = synchronize_to_marker(source, target, "plant", AnimTime{18'000});
  AG_CHECK(synchronized.has_value());
  AG_CHECK_EQ(synchronized->ticks, 30'000);
}

ANIMGRAPH_TEST(state_machine_interrupt_policy_is_explicit_and_stable) {
  StateMachineDefinition definition;
  definition.states = {{StateId{0}, "Idle", AnimTime{100}},
                       {StateId{1}, "Run", AnimTime{100}},
                       {StateId{2}, "Jump", AnimTime{100}}};
  definition.entry = StateId{0};
  definition.transitions = {
      TransitionDefinition{StateId{0}, StateId{1}, ParameterCondition{0, CompareOp::greater, 0.5F},
                           1, AnimTime{100}, std::nullopt, std::nullopt, true},
      TransitionDefinition{StateId{1}, StateId{2}, ParameterCondition{1, CompareOp::greater, 0.5F},
                           1, AnimTime{0}, std::nullopt, std::nullopt, false}};
  auto instance = make_state_machine_instance(definition).value();
  const std::array begin{1.0F, 0.0F};
  AG_CHECK_EQ(update_state_machine(definition, begin, AnimTime{1}, instance)->target.value, 1U);
  const std::array interrupt{1.0F, 1.0F};
  const auto update = update_state_machine(definition, interrupt, AnimTime{1}, instance);
  AG_CHECK(update.has_value());
  AG_CHECK_EQ(update->target.value, 2U);
  AG_CHECK_EQ(update->events[0], std::string{"exit:Run"});
  AG_CHECK_EQ(update->events[1], std::string{"enter:Jump"});
  AG_CHECK_EQ(instance.current.value, 2U);
}

ANIMGRAPH_TEST(root_motion_accumulates_across_loop_seam_and_pose_policy_is_explicit) {
  auto clip = constant_clip("root", 0);
  clip.duration = AnimTime{10};
  clip.tracks[0].translations = {{AnimTime{0}, Vec3{0, 0, 0}}, {AnimTime{10}, Vec3{10, 0, 0}}};
  const auto skeleton = chain_skeleton();
  const auto delta = extract_root_motion(skeleton, clip, JointId{0}, AnimTime{8}, AnimTime{12});
  AG_CHECK(delta.has_value());
  AG_CHECK_NEAR(delta->translation.x, 4.0F, 1.0e-5F);
  LocalPose pose = pose_at(7, 3);
  apply_root_motion_policy(pose, JointId{0}, RootMotionPosePolicy::remove);
  AG_CHECK_EQ(pose.transforms[0], Transform::identity());
}

ANIMGRAPH_TEST(compiled_runtime_executes_blend_additive_layer_and_state_nodes) {
  const auto skeleton = compile_skeleton(RawSkeleton{{RawJoint{
      .name = "root", .parent = std::nullopt, .reference_local = Transform::identity(),
      .inverse_bind = Transform::identity(), .semantic = "root"}}}).value();
  const std::array clips{constant_clip("a", 0), constant_clip("b", 10), constant_clip("c", 20)};

  for (const auto type : {NodeType::blend_1d, NodeType::additive,
                          NodeType::layered_blend_per_bone, NodeType::state_machine}) {
    GraphBuilder builder;
    const auto a = builder.add_node(NodeType::clip_player, "a");
    const auto b = builder.add_node(NodeType::clip_player, "b");
    AG_CHECK(builder.set_clip(a, 0).has_value());
    AG_CHECK(builder.set_clip(b, 1).has_value());
    const auto operation = builder.add_node(type, "operation");
    if (type == NodeType::blend_1d)
      builder.configure_blend_1d(operation, {0.0F, 1.0F}, "weight").value();
    else if (type == NodeType::additive)
      builder.configure_additive(operation, "weight").value();
    else if (type == NodeType::layered_blend_per_bone)
      builder.configure_layered(operation, {}, "weight").value();
    AG_CHECK(builder.connect(PosePin{a, 0}, PosePin{operation, 0}).has_value());
    AG_CHECK(builder.connect(PosePin{b, 0}, PosePin{operation, 1}).has_value());
    const auto output = builder.add_node(NodeType::output, "output");
    AG_CHECK(builder.connect(PosePin{operation, 0}, PosePin{output, 0}).has_value());
    builder.set_output(output);
    builder.add_parameter(GraphParameter{"weight", 0.5F, false});
    const auto graph = compile_graph(builder.build()).value();
    auto instance = make_graph_instance(graph, 1).value();
    const std::array parameters{0.5F};
    const EvaluationContext context{.skeleton = skeleton, .clips = clips, .delta = AnimTime{6'000},
                                    .parameters = parameters, .frame = 1, .generation = 1};
    const auto result = evaluate(context, graph, instance);
    AG_CHECK(result.has_value());
  }

  GraphBuilder builder;
  std::array<NodeId, 3> players;
  for (std::size_t index = 0; index < players.size(); ++index) {
    players[index] = builder.add_node(NodeType::clip_player, "clip" + std::to_string(index));
    AG_CHECK(builder.set_clip(players[index], index).has_value());
  }
  const auto blend = builder.add_node(NodeType::blend_2d, "blend2d");
  builder.configure_blend_2d(blend,
      {GraphPoint2{0,0}, GraphPoint2{1,0}, GraphPoint2{0,1}},
      {std::string{"x"}, std::string{"y"}}).value();
  for (std::uint16_t pin = 0; pin < 3; ++pin)
    AG_CHECK(builder.connect(PosePin{players[pin], 0}, PosePin{blend, pin}).has_value());
  const auto output = builder.add_node(NodeType::output, "output");
  AG_CHECK(builder.connect(PosePin{blend, 0}, PosePin{output, 0}).has_value());
  builder.set_output(output);
  builder.add_parameter(GraphParameter{"x", 0.25F, false});
  builder.add_parameter(GraphParameter{"y", 0.25F, false});
  const auto graph = compile_graph(builder.build()).value();
  auto instance = make_graph_instance(graph, 1).value();
  const std::array parameters{0.25F, 0.25F};
  const EvaluationContext context{.skeleton = skeleton, .clips = clips, .delta = AnimTime{0},
                                  .parameters = parameters, .frame = 1, .generation = 1};
  AG_CHECK_NEAR(evaluate(context, graph, instance)->pose.transforms[0].translation.x, 7.5F, 1.0e-5F);
}

}  // namespace
