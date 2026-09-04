#include "animgraph/graph/graph.hpp"
#include "animgraph/runtime/runtime.hpp"
#include "test_support.hpp"

#include <array>
#include <string>

namespace {

using namespace animgraph;

CompiledSkeleton graph_skeleton() {
  return compile_skeleton(RawSkeleton{{RawJoint{.name = "root", .parent = std::nullopt,
      .reference_local = Transform{Vec3{1, 2, 3}, Quat::identity(), Vec3{1, 1, 1}},
      .inverse_bind = Transform::identity(), .semantic = "root"}}}).value();
}

GraphDescription reference_cache_graph(bool with_dead = false) {
  GraphBuilder builder;
  const auto reference = builder.add_node(NodeType::reference_pose, "reference");
  const auto cache = builder.add_node(NodeType::pose_cache, "cache");
  const auto output = builder.add_node(NodeType::output, "output");
  AG_CHECK(builder.connect(PosePin{reference, 0}, PosePin{cache, 0}).has_value());
  AG_CHECK(builder.connect(PosePin{cache, 0}, PosePin{output, 0}).has_value());
  if (with_dead) static_cast<void>(builder.add_node(NodeType::reference_pose, "dead_clip"));
  builder.set_output(output);
  builder.add_parameter(GraphParameter{.name = "zeta", .default_value = 2.0F, .constant = false});
  builder.add_parameter(GraphParameter{.name = "alpha", .default_value = 1.0F, .constant = false});
  builder.add_parameter(GraphParameter{.name = "folded", .default_value = 3.0F, .constant = true});
  return builder.build();
}

ANIMGRAPH_TEST(graph_compiler_rejects_missing_duplicate_typed_and_cycle_connections) {
  GraphBuilder missing;
  const auto missing_output = missing.add_node(NodeType::output, "output");
  missing.set_output(missing_output);
  AG_CHECK(!compile_graph(missing.build()));

  auto duplicate = reference_cache_graph();
  duplicate.connections.push_back(duplicate.connections.front());
  AG_CHECK(!compile_graph(duplicate));

  auto typed = reference_cache_graph();
  typed.connections.front().type = PinType::scalar;
  AG_CHECK(!compile_graph(typed));

  GraphBuilder cyclic;
  const auto first = cyclic.add_node(NodeType::pose_cache, "first");
  const auto second = cyclic.add_node(NodeType::pose_cache, "second");
  const auto output = cyclic.add_node(NodeType::output, "output");
  AG_CHECK(cyclic.connect(PosePin{first, 0}, PosePin{second, 0}).has_value());
  AG_CHECK(cyclic.connect(PosePin{second, 0}, PosePin{first, 0}).has_value());
  AG_CHECK(cyclic.connect(PosePin{second, 0}, PosePin{output, 0}).has_value());
  cyclic.set_output(output);
  AG_CHECK(!compile_graph(cyclic.build()));
}

ANIMGRAPH_TEST(graph_plan_is_stable_eliminates_dead_nodes_and_sorts_parameters) {
  const auto description = reference_cache_graph(true);
  const auto first = compile_graph(description);
  const auto second = compile_graph(description);
  AG_CHECK(first.has_value() && second.has_value());
  AG_CHECK_EQ(first->instructions.size(), 3U);
  AG_CHECK_EQ(first->parameters[0].name, std::string{"alpha"});
  AG_CHECK_EQ(first->parameters[0].offset, 0U);
  AG_CHECK_EQ(first->parameters[1].name, std::string{"zeta"});
  AG_CHECK_EQ(first->parameters[1].offset, 4U);
  AG_CHECK_EQ(first->constants.size(), 1U);
  AG_CHECK_EQ(first->constants[0].name, std::string{"folded"});
  AG_CHECK_EQ(canonical_plan_json(*first), canonical_plan_json(*second));
  AG_CHECK_EQ(plan_identity(*first), plan_identity(*second));
  AG_CHECK(canonical_plan_json(*first).find("dead_clip") == std::string::npos);
}

ANIMGRAPH_TEST(pose_slot_reuse_reduces_slots_without_changing_output) {
  GraphBuilder builder;
  auto previous = builder.add_node(NodeType::reference_pose, "reference");
  for (int index = 0; index < 5; ++index) {
    const auto cache = builder.add_node(NodeType::pose_cache, "cache_" + std::to_string(index));
    AG_CHECK(builder.connect(PosePin{previous, 0}, PosePin{cache, 0}).has_value());
    previous = cache;
  }
  const auto output = builder.add_node(NodeType::output, "output");
  AG_CHECK(builder.connect(PosePin{previous, 0}, PosePin{output, 0}).has_value());
  builder.set_output(output);
  const auto reused = compile_graph(builder.build(), true);
  const auto oracle = compile_graph(builder.build(), false);
  AG_CHECK(reused.has_value() && oracle.has_value());
  AG_CHECK(reused->pose_slot_count < oracle->pose_slot_count);

  const auto skeleton = graph_skeleton();
  auto reused_instance = make_graph_instance(*reused, skeleton.joints.size()).value();
  auto oracle_instance = make_graph_instance(*oracle, skeleton.joints.size()).value();
  const EvaluationContext context{.skeleton = skeleton, .clips = {}, .delta = AnimTime{0},
                                  .parameters = {}, .frame = 1, .generation = 1};
  const auto a = evaluate(context, *reused, reused_instance);
  const auto b = evaluate(context, *oracle, oracle_instance);
  AG_CHECK(a.has_value() && b.has_value());
  AG_CHECK_EQ(a->pose.transforms, b->pose.transforms);
}

ANIMGRAPH_TEST(pose_cache_hits_and_invalidates_by_parameters_generation_and_character) {
  const auto graph = compile_graph(reference_cache_graph()).value();
  const auto skeleton = graph_skeleton();
  auto first_character = make_graph_instance(graph, skeleton.joints.size()).value();
  auto second_character = make_graph_instance(graph, skeleton.joints.size()).value();
  const std::array params_a{1.0F, 2.0F};
  const std::array params_b{2.0F, 2.0F};
  EvaluationContext context{.skeleton = skeleton, .clips = {}, .delta = AnimTime{0},
                            .parameters = params_a, .frame = 10, .generation = 5};
  const auto first = evaluate(context, graph, first_character).value();
  const auto hit = evaluate(context, graph, first_character).value();
  AG_CHECK_EQ(first.pose_cache_misses, 1U);
  AG_CHECK_EQ(hit.pose_cache_hits, 1U);
  context.parameters = params_b;
  AG_CHECK_EQ(evaluate(context, graph, first_character)->pose_cache_misses, 1U);
  context.parameters = params_a;
  ++context.generation;
  AG_CHECK_EQ(evaluate(context, graph, first_character)->pose_cache_misses, 1U);
  AG_CHECK_EQ(evaluate(context, graph, second_character)->pose_cache_misses, 1U);
  AG_CHECK_EQ(hit.pose.transforms[0].translation, (Vec3{1, 2, 3}));
}

ANIMGRAPH_TEST(clip_player_uses_compiled_schedule_and_per_instance_time) {
  GraphBuilder builder;
  const auto player = builder.add_node(NodeType::clip_player, "walk");
  AG_CHECK(builder.set_clip(player, 0).has_value());
  const auto output = builder.add_node(NodeType::output, "output");
  AG_CHECK(builder.connect(PosePin{player, 0}, PosePin{output, 0}).has_value());
  builder.set_output(output);
  const auto graph = compile_graph(builder.build()).value();
  const auto skeleton = graph_skeleton();
  AnimationClip clip;
  clip.name = "walk";
  clip.duration = AnimTime{48'000};
  clip.mode = ClipPlaybackMode::clamp;
  JointTrack track;
  track.joint = JointId{0};
  track.translations = {{AnimTime{0}, Vec3{0, 0, 0}}, {AnimTime{48'000}, Vec3{10, 0, 0}}};
  clip.tracks.push_back(track);
  const std::array clips{clip};
  auto instance = make_graph_instance(graph, skeleton.joints.size()).value();
  const EvaluationContext context{.skeleton = skeleton, .clips = clips,
                                  .delta = AnimTime{24'000}, .parameters = {},
                                  .frame = 1, .generation = 1};
  const auto result = evaluate(context, graph, instance);
  AG_CHECK(result.has_value());
  AG_CHECK_NEAR(result->pose.transforms[0].translation.x, 5.0F, 1.0e-5F);
  AG_CHECK_EQ(instance.clip_times[0].ticks, 24'000);
}

}  // namespace
