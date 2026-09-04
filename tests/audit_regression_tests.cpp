#include "animgraph/asset/codec.hpp"
#include "animgraph/compression/compression.hpp"
#include "animgraph/ik/two_bone_ik.hpp"
#include "animgraph/runtime/blend.hpp"
#include "animgraph/runtime/runtime.hpp"
#include "animgraph/runtime/state_machine.hpp"
#include "test_support.hpp"

#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>

namespace {

using namespace animgraph;

CompiledSkeleton one_joint() {
  return compile_skeleton(RawSkeleton{{RawJoint{.name = "root", .parent = std::nullopt,
      .reference_local = Transform::identity(), .inverse_bind = Transform::identity(),
      .semantic = "root"}}}).value();
}

AnimationClip event_clip(std::string name, std::string event_name) {
  AnimationClip clip;
  clip.name = std::move(name); clip.duration = AnimTime{10}; clip.mode = ClipPlaybackMode::loop;
  JointTrack track; track.joint = JointId{0};
  track.translations = {{AnimTime{0}, Vec3{}}, {AnimTime{10}, Vec3{10, 0, 0}}};
  clip.tracks.push_back(std::move(track));
  clip.events.push_back({AnimTime{5}, std::move(event_name), 0});
  return clip;
}

AnimationClip marked_clip(std::string name, std::string event_name,
                          std::string marker_name) {
  auto clip = event_clip(std::move(name), std::move(event_name));
  clip.markers.push_back({AnimTime{5}, std::move(marker_name)});
  return clip;
}

CompiledGraph clip_graph(bool cache) {
  GraphBuilder builder;
  const auto player = builder.add_node(NodeType::clip_player, "player");
  builder.set_clip(player, 0).value();
  NodeId previous = player;
  if (cache) {
    const auto node = builder.add_node(NodeType::pose_cache, "cache");
    builder.connect(PosePin{previous, 0}, PosePin{node, 0}).value();
    previous = node;
  }
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{previous, 0}, PosePin{output, 0}).value();
  builder.set_output(output);
  return compile_graph(builder.build()).value();
}

std::uint32_t crc32(const std::vector<std::byte>& bytes, std::size_t zero_offset) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto input = index >= zero_offset && index < zero_offset + 4
        ? std::uint8_t{0} : std::to_integer<std::uint8_t>(bytes[index]);
    crc ^= input;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}
void patch_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned byte = 0; byte < 4; ++byte)
    bytes[offset + byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU);
}

ANIMGRAPH_TEST(near_assertion_rejects_nan) {
  bool rejected = false;
  try {
    AG_CHECK_NEAR(std::numeric_limits<float>::quiet_NaN(), 0.0F, 1.0F);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  AG_CHECK(rejected);
}

ANIMGRAPH_TEST(nonuniform_rotated_trs_operations_fail_closed) {
  const Transform scaled{Vec3{1, 0, 0}, *from_axis_angle(Vec3{0, 0, 1}, 0.5F), Vec3{2, 1, 1}};
  const Transform rotated{Vec3{}, *from_axis_angle(Vec3{0, 1, 0}, 0.25F), Vec3{1, 1, 1}};
  AG_CHECK(!inverse(scaled));
  AG_CHECK(!compose(scaled, rotated));
}

ANIMGRAPH_TEST(blend2d_aligns_antipodal_quaternions_from_nonzero_weight_reference) {
  LocalPose first{{Transform{Vec3{}, *from_axis_angle(Vec3{1, 0, 0}, 3.14159265F), Vec3{1, 1, 1}}}};
  LocalPose identity{{Transform::identity()}};
  LocalPose negative_identity{{Transform{Vec3{}, Quat{0, 0, 0, -1}, Vec3{1, 1, 1}}}};
  const auto result = evaluate_blend_2d(std::array{
      Blend2DSample{Vec2{0, 0}, first}, Blend2DSample{Vec2{1, 0}, identity},
      Blend2DSample{Vec2{0, 1}, negative_identity}}, Vec2{0.5F, 0.5F});
  AG_CHECK(result.has_value());
  AG_CHECK(angular_distance(result->pose.transforms[0].rotation, Quat::identity()) < 1.0e-4F);
}

ANIMGRAPH_TEST(state_exit_time_detects_threshold_crossed_by_large_delta) {
  StateMachineDefinition definition;
  definition.states = {{StateId{0}, "A", AnimTime{100}}, {StateId{1}, "B", AnimTime{100}}};
  definition.entry = StateId{0};
  definition.transitions = {TransitionDefinition{StateId{0}, StateId{1},
      ParameterCondition{0, CompareOp::greater, 0.0F}, 1, AnimTime{0}, 0.5F,
      std::nullopt, false}};
  auto instance = make_state_machine_instance(definition).value();
  const std::array parameters{1.0F};
  const auto update = update_state_machine(definition, parameters, AnimTime{120}, instance);
  AG_CHECK(update.has_value());
  AG_CHECK_EQ(update->target.value, 1U);
}

ANIMGRAPH_TEST(evaluator_rejects_corrupted_inner_instance_and_output_layout) {
  const auto skeleton = one_joint();
  auto graph = clip_graph(false);
  auto instance = make_graph_instance(graph, 1).value();
  instance.pose_slots[0].transforms.clear();
  AG_CHECK(!validate_instance_layout(graph, instance, 1));
  instance = make_graph_instance(graph, 1).value();
  graph.output_slot = PoseSlot{graph.pose_slot_count};
  AG_CHECK(!validate_instance_layout(graph, instance, 1));
}

ANIMGRAPH_TEST(ik_rejects_finite_components_whose_norm_overflows) {
  const auto skeleton = compile_skeleton(RawSkeleton{{
      RawJoint{.name="r",.parent=std::nullopt,.reference_local=Transform::identity(),.inverse_bind=Transform::identity(),.semantic=std::nullopt},
      RawJoint{.name="m",.parent=0,.reference_local=Transform{Vec3{1,0,0},Quat::identity(),Vec3{1,1,1}},.inverse_bind=Transform::identity(),.semantic=std::nullopt},
      RawJoint{.name="e",.parent=1,.reference_local=Transform{Vec3{1,0,0},Quat::identity(),Vec3{1,1,1}},.inverse_bind=Transform::identity(),.semantic=std::nullopt}}}).value();
  LocalPose pose;
  for (const auto& joint : skeleton.joints) pose.transforms.push_back(joint.reference_local);
  const float huge = std::numeric_limits<float>::max();
  AG_CHECK(!solve_two_bone_ik(skeleton, pose,
      TwoBoneIkRequest{JointId{0},JointId{1},JointId{2},Vec3{huge,huge,huge},Vec3{0,0,1},1.0F,std::nullopt}));
}

ANIMGRAPH_TEST(graph_asset_rejects_crc_valid_non_json_payload) {
  GraphBuilder builder;
  const auto reference = builder.add_node(NodeType::reference_pose, "reference");
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{reference,0}, PosePin{output,0}).value(); builder.set_output(output);
  auto bytes = encode_graph_plan(compile_graph(builder.build()).value()).value();
  for (std::size_t index = 36; index < bytes.size(); ++index) bytes[index] = std::byte{'x'};
  bytes[36] = std::byte{'{'}; bytes.back() = std::byte{'}'};
  patch_u32(bytes, 32, 0); patch_u32(bytes, 32, crc32(bytes, 32));
  AG_CHECK(!decode_graph_plan(bytes));
}

ANIMGRAPH_TEST(rotation_compression_checks_between_original_key_times) {
  AnimationClip clip; clip.name="turn"; clip.duration=AnimTime{100}; clip.mode=ClipPlaybackMode::clamp;
  JointTrack track; track.joint=JointId{0};
  track.rotations = {{AnimTime{0}, *from_axis_angle(Vec3{1,0,0}, 0.0F)},
                     {AnimTime{50}, *from_axis_angle(Vec3{1,0,0}, -0.34906585F)},
                     {AnimTime{100}, *from_axis_angle(Vec3{1,0,0}, 2.9496064F)}};
  clip.tracks.push_back(track);
  CompressionReport report;
  const CompressionSettings settings{0.0F, 1.83F, 0.0F};
  const auto compressed = compress_clip(clip, settings, report);
  AG_CHECK(compressed.has_value());
  float maximum = 0.0F;
  const auto skeleton = one_joint();
  for (std::int64_t tick = 0; tick <= 100; ++tick) {
    const auto raw = sample_clip(skeleton, clip, AnimTime{tick})->pose.transforms[0].rotation;
    const auto reduced = sample_clip(skeleton, compressed->clip, AnimTime{tick})->pose.transforms[0].rotation;
    maximum = std::max(maximum, angular_distance(raw, reduced));
  }
  AG_CHECK(maximum <= settings.rotation_radians_error + 1.0e-4F);
}

ANIMGRAPH_TEST(runtime_events_follow_active_branch_and_preserve_loop_occurrences) {
  const auto skeleton = one_joint();
  const std::array clips{event_clip("a","active"), event_clip("b","inactive")};
  GraphBuilder builder;
  const auto a=builder.add_node(NodeType::clip_player,"a"); const auto b=builder.add_node(NodeType::clip_player,"b");
  builder.set_clip(a,0).value(); builder.set_clip(b,1).value();
  const auto state=builder.add_node(NodeType::state_machine,"state");
  builder.connect(PosePin{a,0},PosePin{state,0}).value(); builder.connect(PosePin{b,0},PosePin{state,1}).value();
  const auto output=builder.add_node(NodeType::output,"output"); builder.connect(PosePin{state,0},PosePin{output,0}).value();
  builder.set_output(output); builder.add_parameter(GraphParameter{"weight",0,false});
  const auto graph=compile_graph(builder.build()).value(); auto instance=make_graph_instance(graph,1).value();
  const std::array parameters{0.0F};
  const EvaluationContext context{skeleton,clips,AnimTime{5},parameters,1,1};
  const auto result=evaluate(context,graph,instance).value();
  AG_CHECK_EQ(result.events.size(),1U); AG_CHECK_EQ(result.events[0].name,std::string{"active"});

  const auto loop_graph=clip_graph(false); auto loop_instance=make_graph_instance(loop_graph,1).value();
  const std::array one_clip{event_clip("loop","pulse")};
  const EvaluationContext loop_context{skeleton,one_clip,AnimTime{25},{},1,1};
  AG_CHECK_EQ(evaluate(loop_context,loop_graph,loop_instance)->events.size(),3U);
}

ANIMGRAPH_TEST(pose_cache_same_frame_hit_does_not_advance_upstream_time) {
  const auto skeleton=one_joint(); const auto graph=clip_graph(true);
  const std::array clips{event_clip("clip","event")}; auto instance=make_graph_instance(graph,1).value();
  const EvaluationContext context{skeleton,clips,AnimTime{2},{},7,1};
  const auto first=evaluate(context,graph,instance).value(); const auto time=instance.clip_times[0];
  const auto second=evaluate(context,graph,instance).value();
  AG_CHECK_EQ(instance.clip_times[0],time); AG_CHECK_EQ(second.pose_cache_hits,1U);
  AG_CHECK_EQ(first.pose.transforms,second.pose.transforms);
}

ANIMGRAPH_TEST(compiled_nodes_consume_bound_configuration_instead_of_global_defaults) {
  const auto skeleton = one_joint();
  std::array clips{event_clip("low","low"), event_clip("high","high")};
  clips[0].tracks[0].translations[0].value.x = 0.0F;
  clips[0].tracks[0].translations[1].value.x = 0.0F;
  clips[1].tracks[0].translations[0].value.x = 10.0F;
  clips[1].tracks[0].translations[1].value.x = 10.0F;
  GraphBuilder builder;
  const auto low=builder.add_node(NodeType::clip_player,"low");
  const auto high=builder.add_node(NodeType::clip_player,"high");
  builder.set_clip(low,0).value(); builder.set_clip(high,1).value();
  const auto blend=builder.add_node(NodeType::blend_1d,"configured");
  builder.configure_blend_1d(blend, {2.0F,4.0F}, "").value();
  builder.connect(ValuePin{std::string{"speed"}}, ValuePin{blend,0}).value();
  builder.connect(PosePin{low,0},PosePin{blend,0}).value();
  builder.connect(PosePin{high,0},PosePin{blend,1}).value();
  const auto output=builder.add_node(NodeType::output,"output");
  builder.connect(PosePin{blend,0},PosePin{output,0}).value(); builder.set_output(output);
  builder.add_parameter(GraphParameter{"speed",0.0F,false});
  const auto graph=compile_graph(builder.build()).value(); auto instance=make_graph_instance(graph,1).value();
  const std::array parameters{3.0F};
  const EvaluationContext context{skeleton,clips,AnimTime{0},parameters,1,1};
  AG_CHECK_NEAR(evaluate(context,graph,instance)->pose.transforms[0].translation.x,5.0F,1.0e-5F);
  AG_CHECK_EQ(graph.instructions[2].parameter_indices.size(),1U);
}

ANIMGRAPH_TEST(compiled_layer_mask_and_state_marker_configuration_drive_runtime) {
  const auto skeleton = compile_skeleton(RawSkeleton{{
      RawJoint{.name="root",.parent=std::nullopt,.reference_local=Transform::identity(),.inverse_bind=Transform::identity(),.semantic=std::nullopt},
      RawJoint{.name="child",.parent=0,.reference_local=Transform::identity(),.inverse_bind=Transform::identity(),.semantic=std::nullopt}}}).value();
  auto base = event_clip("base","base_event");
  auto layer = event_clip("layer","layer_event");
  base.tracks[0].translations = {{AnimTime{0},Vec3{}},{AnimTime{10},Vec3{}}};
  layer.tracks[0].translations = {{AnimTime{0},Vec3{10,0,0}},{AnimTime{10},Vec3{10,0,0}}};
  JointTrack base_child; base_child.joint=JointId{1}; base_child.translations={{AnimTime{0},Vec3{}},{AnimTime{10},Vec3{}}};
  JointTrack layer_child; layer_child.joint=JointId{1}; layer_child.translations={{AnimTime{0},Vec3{10,0,0}},{AnimTime{10},Vec3{10,0,0}}};
  base.tracks.push_back(base_child); layer.tracks.push_back(layer_child);
  std::array clips{base,layer};
  GraphBuilder builder;
  const auto a=builder.add_node(NodeType::clip_player,"base"); const auto b=builder.add_node(NodeType::clip_player,"layer");
  builder.set_clip(a,0).value(); builder.set_clip(b,1).value();
  const auto layered=builder.add_node(NodeType::layered_blend_per_bone,"mask");
  builder.configure_layered(layered,{0.0F,1.0F},"weight").value();
  builder.connect(PosePin{a,0},PosePin{layered,0}).value(); builder.connect(PosePin{b,0},PosePin{layered,1}).value();
  const auto output=builder.add_node(NodeType::output,"output"); builder.connect(PosePin{layered,0},PosePin{output,0}).value();
  builder.set_output(output); builder.add_parameter(GraphParameter{"weight",1,false});
  auto graph=compile_graph(builder.build()).value(); auto instance=make_graph_instance(graph,2).value();
  const std::array weight{1.0F}; const EvaluationContext context{skeleton,clips,AnimTime{0},weight,1,1};
  const auto result=evaluate(context,graph,instance).value();
  AG_CHECK_NEAR(result.pose.transforms[0].translation.x,0.0F,1.0e-5F);
  AG_CHECK_NEAR(result.pose.transforms[1].translation.x,10.0F,1.0e-5F);

  clips[0].markers={{AnimTime{2},"sync"}}; clips[1].markers={{AnimTime{7},"sync"}};
  GraphBuilder state_builder;
  const auto s0=state_builder.add_node(NodeType::clip_player,"s0"); const auto s1=state_builder.add_node(NodeType::clip_player,"s1");
  state_builder.set_clip(s0,0).value(); state_builder.set_clip(s1,1).value();
  const auto state=state_builder.add_node(NodeType::state_machine,"state");
  StateMachineNodeConfig state_config;
  state_config.definition.states={{StateId{0},"A",AnimTime{10}},{StateId{1},"B",AnimTime{10}}};
  state_config.definition.entry=StateId{0};
  state_config.definition.transitions={TransitionDefinition{StateId{0},StateId{1},
      ParameterCondition{0,CompareOp::greater,0.5F},1,AnimTime{2},std::nullopt,std::string{"sync"},false}};
  state_config.sync_clip_indices={0U,1U}; state_builder.configure_state_machine(state,std::move(state_config)).value();
  state_builder.connect(PosePin{s0,0},PosePin{state,0}).value(); state_builder.connect(PosePin{s1,0},PosePin{state,1}).value();
  const auto state_output=state_builder.add_node(NodeType::output,"output"); state_builder.connect(PosePin{state,0},PosePin{state_output,0}).value();
  state_builder.set_output(state_output); state_builder.add_parameter(GraphParameter{"speed",0,false});
  graph=compile_graph(state_builder.build()).value(); instance=make_graph_instance(graph,2).value();
  const std::array speed{1.0F}; const EvaluationContext state_context{skeleton,clips,AnimTime{5},speed,1,1};
  const auto state_result=evaluate(state_context,graph,instance).value();
  AG_CHECK(std::ranges::find(state_result.sync_markers,std::string{"sync"})!=state_result.sync_markers.end());
  AG_CHECK_EQ(instance.clip_times[1].ticks,0);
}

ANIMGRAPH_TEST(runtime_occurrences_deduplicate_dag_merges_and_follow_contributing_branches) {
  const auto skeleton = one_joint();
  const std::array clips{marked_clip("a", "active", "active_marker"),
                         marked_clip("b", "inactive", "inactive_marker")};

  GraphBuilder merge_builder;
  const auto player = merge_builder.add_node(NodeType::clip_player, "player");
  merge_builder.set_clip(player, 0).value();
  const auto blend = merge_builder.add_node(NodeType::blend_1d, "merge");
  merge_builder.connect(PosePin{player,0}, PosePin{blend,0}).value();
  merge_builder.connect(PosePin{player,0}, PosePin{blend,1}).value();
  const auto merge_output = merge_builder.add_node(NodeType::output, "output");
  merge_builder.connect(PosePin{blend,0}, PosePin{merge_output,0}).value();
  merge_builder.set_output(merge_output);
  const auto merge_graph = compile_graph(merge_builder.build()).value();
  auto merge_instance = make_graph_instance(merge_graph, 1).value();
  const EvaluationContext merge_context{skeleton, std::span{clips}.first(1),
                                         AnimTime{5}, {}, 1, 1};
  const auto merged = evaluate(merge_context, merge_graph, merge_instance).value();
  AG_CHECK_EQ(merged.event_occurrences.size(), 1U);
  AG_CHECK_EQ(merged.sync_marker_occurrences.size(), 1U);

  GraphBuilder state_builder;
  const auto active = state_builder.add_node(NodeType::clip_player, "active");
  const auto inactive = state_builder.add_node(NodeType::clip_player, "inactive");
  state_builder.set_clip(active, 0).value();
  state_builder.set_clip(inactive, 1).value();
  const auto state = state_builder.add_node(NodeType::state_machine, "state");
  state_builder.connect(PosePin{active,0}, PosePin{state,0}).value();
  state_builder.connect(PosePin{inactive,0}, PosePin{state,1}).value();
  const auto output = state_builder.add_node(NodeType::output, "output");
  state_builder.connect(PosePin{state,0}, PosePin{output,0}).value();
  state_builder.set_output(output);
  state_builder.add_parameter(GraphParameter{"weight", 0.0F, false});
  const auto graph = compile_graph(state_builder.build()).value();
  auto instance = make_graph_instance(graph, 1).value();
  const std::array parameters{0.0F};
  const auto result = evaluate(EvaluationContext{skeleton, clips, AnimTime{5}, parameters, 1, 1},
                               graph, instance).value();
  AG_CHECK_EQ(result.events.size(), 1U);
  AG_CHECK_EQ(result.events[0].name, std::string{"active"});
  AG_CHECK_EQ(result.sync_marker_occurrences.size(), 1U);
  AG_CHECK_EQ(result.sync_marker_occurrences[0].marker.name, std::string{"active_marker"});
  AG_CHECK_EQ(result.sync_marker_occurrences[0].source_node, active);
}

ANIMGRAPH_TEST(layer_with_zero_effective_mask_does_not_contribute_events_or_markers) {
  const auto skeleton = one_joint();
  const std::array clips{marked_clip("base", "base_event", "base_marker"),
                         marked_clip("layer", "layer_event", "layer_marker")};
  GraphBuilder builder;
  const auto base = builder.add_node(NodeType::clip_player, "base");
  const auto layer = builder.add_node(NodeType::clip_player, "layer");
  builder.set_clip(base, 0).value(); builder.set_clip(layer, 1).value();
  const auto layered = builder.add_node(NodeType::layered_blend_per_bone, "layered");
  builder.configure_layered(layered, {0.0F}, "", 1.0F).value();
  builder.connect(PosePin{base,0}, PosePin{layered,0}).value();
  builder.connect(PosePin{layer,0}, PosePin{layered,1}).value();
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{layered,0}, PosePin{output,0}).value(); builder.set_output(output);
  const auto graph = compile_graph(builder.build()).value();
  auto instance = make_graph_instance(graph, 1).value();
  const auto result = evaluate(EvaluationContext{skeleton, clips, AnimTime{5}, {}, 1, 1},
                               graph, instance).value();
  AG_CHECK_EQ(result.events.size(), 1U);
  AG_CHECK_EQ(result.events[0].name, std::string{"base_event"});
  AG_CHECK_EQ(result.sync_markers.size(), 1U);
  AG_CHECK_EQ(result.sync_markers[0], std::string{"base_marker"});
}

ANIMGRAPH_TEST(state_exit_zero_transitions_immediately_and_marker_sync_precedes_sampling) {
  StateMachineDefinition definition;
  definition.states={{StateId{0},"A",AnimTime{10}},{StateId{1},"B",AnimTime{10}}};
  definition.entry=StateId{0};
  definition.transitions={TransitionDefinition{StateId{0},StateId{1},
      ParameterCondition{0,CompareOp::greater,0.5F},1,AnimTime{0},0.0F,std::nullopt,false}};
  auto state_instance=make_state_machine_instance(definition).value();
  const std::array speed{1.0F};
  AG_CHECK_EQ(update_state_machine(definition,speed,AnimTime{1},state_instance)->target,StateId{1});

  const auto skeleton=one_joint();
  auto source=event_clip("source","source_mid");
  auto target=event_clip("target","target_mid");
  source.markers={{AnimTime{2},"sync"}}; target.markers={{AnimTime{7},"sync"}};
  target.tracks[0].translations={{AnimTime{0},Vec3{}},{AnimTime{10},Vec3{10,0,0}}};
  const std::array clips{source,target};
  GraphBuilder builder;
  const auto source_player=builder.add_node(NodeType::clip_player,"source");
  const auto target_player=builder.add_node(NodeType::clip_player,"target");
  builder.set_clip(source_player,0).value(); builder.set_clip(target_player,1).value();
  const auto state=builder.add_node(NodeType::state_machine,"state");
  StateMachineNodeConfig config;
  config.definition.states={{StateId{0},"A",AnimTime{10}},{StateId{1},"B",AnimTime{10}}};
  config.definition.entry=StateId{0};
  config.definition.transitions={TransitionDefinition{StateId{0},StateId{1},
      ParameterCondition{0,CompareOp::greater,0.5F},1,AnimTime{0},std::nullopt,std::string{"sync"},false}};
  config.sync_clip_indices={0U,1U}; builder.configure_state_machine(state,std::move(config)).value();
  builder.connect(PosePin{source_player,0},PosePin{state,0}).value();
  builder.connect(PosePin{target_player,0},PosePin{state,1}).value();
  const auto output=builder.add_node(NodeType::output,"output");
  builder.connect(PosePin{state,0},PosePin{output,0}).value(); builder.set_output(output);
  builder.add_parameter(GraphParameter{"speed",0,false});
  const auto graph=compile_graph(builder.build()).value(); auto instance=make_graph_instance(graph,1).value();
  const auto result=evaluate(EvaluationContext{skeleton,clips,AnimTime{5},speed,1,1},graph,instance).value();
  AG_CHECK_EQ(instance.clip_times[1],AnimTime{0});
  AG_CHECK_NEAR(result.pose.transforms[0].translation.x,0.0F,1.0e-6F);
  AG_CHECK(std::ranges::none_of(result.events,[](const AnimationEvent& event){return event.name=="target_mid";}));

  auto delayed_instance=make_graph_instance(graph,1).value();
  const std::array stopped{0.0F};
  AG_CHECK(evaluate(EvaluationContext{skeleton,clips,AnimTime{5},stopped,1,1},graph,delayed_instance));
  AG_CHECK(evaluate(EvaluationContext{skeleton,clips,AnimTime{5},stopped,2,1},graph,delayed_instance));
  const auto delayed=evaluate(EvaluationContext{skeleton,clips,AnimTime{5},speed,3,1},graph,delayed_instance);
  AG_CHECK(delayed.has_value());
  if (delayed_instance.clip_times[1] != AnimTime{10}) {
    animgraph::test::fail("delayed target clock == 10", __FILE__, __LINE__,
                         std::to_string(delayed_instance.clip_times[1].ticks));
  }
  AG_CHECK_NEAR(delayed->pose.transforms[0].translation.x,0.0F,1.0e-6F);
  AG_CHECK(std::ranges::none_of(delayed->events,[](const AnimationEvent& event){return event.name=="target_mid";}));
}

ANIMGRAPH_TEST(state_sync_uses_the_exact_selected_transition_identity) {
  const auto skeleton=one_joint();
  auto source=event_clip("source","source_event");
  auto target=event_clip("target","target_event");
  source.markers={{AnimTime{1},"high"},{AnimTime{2},"low"}};
  target.markers={{AnimTime{7},"low"},{AnimTime{9},"high"}};
  const std::array clips{source,target};
  GraphBuilder builder;
  const auto a=builder.add_node(NodeType::clip_player,"a");
  const auto b=builder.add_node(NodeType::clip_player,"b");
  builder.set_clip(a,0).value(); builder.set_clip(b,1).value();
  const auto state=builder.add_node(NodeType::state_machine,"state");
  StateMachineNodeConfig config;
  config.definition.states={{StateId{0},"A",AnimTime{10}},{StateId{1},"B",AnimTime{10}}};
  config.definition.entry=StateId{0};
  config.definition.transitions={
      TransitionDefinition{StateId{0},StateId{1},ParameterCondition{0,CompareOp::greater,0.5F},
                           1,AnimTime{0},std::nullopt,std::string{"low"},false},
      TransitionDefinition{StateId{0},StateId{1},ParameterCondition{0,CompareOp::greater,0.5F},
                           2,AnimTime{0},std::nullopt,std::string{"high"},false}};
  config.sync_clip_indices={0U,1U}; builder.configure_state_machine(state,std::move(config)).value();
  builder.connect(PosePin{a,0},PosePin{state,0}).value(); builder.connect(PosePin{b,0},PosePin{state,1}).value();
  const auto output=builder.add_node(NodeType::output,"output");
  builder.connect(PosePin{state,0},PosePin{output,0}).value(); builder.set_output(output);
  builder.add_parameter(GraphParameter{"speed",0,false});
  const auto graph=compile_graph(builder.build()).value(); auto instance=make_graph_instance(graph,1).value();
  const std::array speed{1.0F};
  const auto result=evaluate(EvaluationContext{skeleton,clips,AnimTime{5},speed,1,1},graph,instance);
  AG_CHECK(result.has_value());
  AG_CHECK_EQ(instance.clip_times[1],AnimTime{3});
  AG_CHECK(std::ranges::find(result->sync_markers,std::string{"high"})!=result->sync_markers.end());
  AG_CHECK(std::ranges::find(result->sync_markers,std::string{"low"})==result->sync_markers.end());
}

ANIMGRAPH_TEST(graph_compiler_rejects_duplicate_value_bindings_and_invalid_geometry_or_limits) {
  GraphBuilder duplicate;
  const auto a=duplicate.add_node(NodeType::reference_pose,"a");
  const auto b=duplicate.add_node(NodeType::reference_pose,"b");
  const auto blend=duplicate.add_node(NodeType::blend_1d,"blend");
  duplicate.connect(PosePin{a,0},PosePin{blend,0}).value();
  duplicate.connect(PosePin{b,0},PosePin{blend,1}).value();
  AG_CHECK(duplicate.connect(ValuePin{std::string{"first"}},ValuePin{blend,0}).has_value());
  AG_CHECK(!duplicate.connect(ValuePin{std::string{"second"}},ValuePin{blend,0}));

  GraphBuilder degenerate;
  const auto p0=degenerate.add_node(NodeType::reference_pose,"p0");
  const auto p1=degenerate.add_node(NodeType::reference_pose,"p1");
  const auto p2=degenerate.add_node(NodeType::reference_pose,"p2");
  const auto b2=degenerate.add_node(NodeType::blend_2d,"b2");
  degenerate.configure_blend_2d(b2,{GraphPoint2{0,0},GraphPoint2{1,1},GraphPoint2{2,2}},{}).value();
  degenerate.connect(PosePin{p0,0},PosePin{b2,0}).value();
  degenerate.connect(PosePin{p1,0},PosePin{b2,1}).value();
  degenerate.connect(PosePin{p2,0},PosePin{b2,2}).value();
  const auto out=degenerate.add_node(NodeType::output,"out");
  degenerate.connect(PosePin{b2,0},PosePin{out,0}).value(); degenerate.set_output(out);
  AG_CHECK(!compile_graph(degenerate.build()));

  GraphBuilder invalid_ik;
  const auto input=invalid_ik.add_node(NodeType::reference_pose,"input");
  const auto ik=invalid_ik.add_node(NodeType::two_bone_ik,"ik");
  TwoBoneIkNodeConfig ik_config;
  ik_config.limit=JointLimit{2.0F,1.0F};
  invalid_ik.configure_two_bone_ik(ik,std::move(ik_config)).value();
  invalid_ik.connect(PosePin{input,0},PosePin{ik,0}).value();
  const auto ik_out=invalid_ik.add_node(NodeType::output,"out");
  invalid_ik.connect(PosePin{ik,0},PosePin{ik_out,0}).value(); invalid_ik.set_output(ik_out);
  AG_CHECK(!compile_graph(invalid_ik.build()));
}

ANIMGRAPH_TEST(evaluator_rejects_type_specific_instruction_arity_corruption) {
  const auto skeleton=one_joint();
  const std::array clips{event_clip("a","event")};
  GraphBuilder builder;
  const auto a=builder.add_node(NodeType::clip_player,"a");
  builder.set_clip(a,0).value();
  const auto blend=builder.add_node(NodeType::blend_1d,"blend");
  builder.connect(PosePin{a,0},PosePin{blend,0}).value();
  builder.connect(PosePin{a,0},PosePin{blend,1}).value();
  const auto output=builder.add_node(NodeType::output,"output");
  builder.connect(PosePin{blend,0},PosePin{output,0}).value(); builder.set_output(output);
  auto graph=compile_graph(builder.build()).value();
  auto instance=make_graph_instance(graph,1).value();
  graph.instructions[1].inputs.clear();
  const auto validated=validate_instance_layout(graph,instance,1);
  AG_CHECK(!validated);
  AG_CHECK_EQ(validated.error().code,ErrorCode::graph);
}

ANIMGRAPH_TEST(graph_asset_rejects_schema_spoof_even_with_valid_crc) {
  const std::string spoof=R"({"version":0,"instructions":[],"junk":{"type":"bogus"}})";
  GraphBuilder builder;
  const auto reference=builder.add_node(NodeType::reference_pose,"reference");
  const auto output=builder.add_node(NodeType::output,"output");
  builder.connect(PosePin{reference,0},PosePin{output,0}).value(); builder.set_output(output);
  auto bytes=encode_graph_plan(compile_graph(builder.build()).value()).value();
  bytes.resize(36);
  for (const char value : spoof) bytes.push_back(static_cast<std::byte>(value));
  patch_u32(bytes,16,static_cast<std::uint32_t>(bytes.size()));
  patch_u32(bytes,24,static_cast<std::uint32_t>(spoof.size()));
  patch_u32(bytes,28,1U); patch_u32(bytes,32,0U); patch_u32(bytes,32,crc32(bytes,32));
  const auto decoded=decode_graph_plan(bytes);
  AG_CHECK(!decoded);
  AG_CHECK_EQ(decoded.error().code,ErrorCode::invalid_format);
}

ANIMGRAPH_TEST(graph_asset_round_trips_negative_state_transition_priority) {
  GraphBuilder builder;
  const auto a=builder.add_node(NodeType::reference_pose,"a");
  const auto b=builder.add_node(NodeType::reference_pose,"b");
  const auto state=builder.add_node(NodeType::state_machine,"state");
  StateMachineNodeConfig config;
  config.definition.states={{StateId{0},"A",AnimTime{10}},{StateId{1},"B",AnimTime{10}}};
  config.definition.entry=StateId{0};
  config.definition.transitions={TransitionDefinition{StateId{0},StateId{1},
      ParameterCondition{0,CompareOp::greater,0.5F},-1,AnimTime{0},std::nullopt,std::nullopt,false}};
  builder.configure_state_machine(state,std::move(config)).value();
  builder.connect(PosePin{a,0},PosePin{state,0}).value(); builder.connect(PosePin{b,0},PosePin{state,1}).value();
  const auto output=builder.add_node(NodeType::output,"output");
  builder.connect(PosePin{state,0},PosePin{output,0}).value(); builder.set_output(output);
  builder.add_parameter(GraphParameter{"speed",0,false});
  const auto encoded=encode_graph_plan(compile_graph(builder.build()).value()).value();
  const auto decoded=decode_graph_plan(encoded);
  AG_CHECK(decoded.has_value());
}

ANIMGRAPH_TEST(rotation_compression_never_accepts_an_unsampled_error_peak) {
  AnimationClip clip; clip.name="audit_peak"; clip.duration=AnimTime{6400}; clip.mode=ClipPlaybackMode::clamp;
  JointTrack track; track.joint=JointId{0};
  track.rotations={
      {AnimTime{0},normalize(Quat{-0.02771932F,0.57150980F,-0.52126180F,-0.63316216F}).value()},
      {AnimTime{3200},normalize(Quat{-0.56156297F,0.37527490F,0.43676058F,0.59418511F}).value()},
      {AnimTime{6400},normalize(Quat{0.46086126F,0.18599296F,0.82562565F,-0.26712508F}).value()}};
  clip.tracks.push_back(track);
  CompressionReport report;
  const CompressionSettings settings{0.0F,3.08F,0.0F};
  const auto compressed=compress_clip(clip,settings,report).value();
  const auto skeleton=one_joint();
  float maximum=0.0F;
  for (std::int64_t tick=0;tick<=6400;++tick) {
    const auto raw=sample_clip(skeleton,clip,AnimTime{tick})->pose.transforms[0].rotation;
    const auto reduced=sample_clip(skeleton,compressed.clip,AnimTime{tick})->pose.transforms[0].rotation;
    maximum=std::max(maximum,angular_distance(raw,reduced));
  }
  AG_CHECK(maximum<=settings.rotation_radians_error+1.0e-4F);
  AG_CHECK(report.max_rotation_error+1.0e-4F>=maximum);
}

}  // namespace
