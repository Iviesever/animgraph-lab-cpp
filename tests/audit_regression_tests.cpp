#include "animgraph/asset/codec.hpp"
#include "animgraph/compression/compression.hpp"
#include "animgraph/ik/two_bone_ik.hpp"
#include "animgraph/runtime/blend.hpp"
#include "animgraph/runtime/runtime.hpp"
#include "animgraph/runtime/state_machine.hpp"
#include "test_support.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

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

}  // namespace
