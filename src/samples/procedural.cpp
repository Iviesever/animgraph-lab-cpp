#include "animgraph/samples/procedural.hpp"

#include "animgraph/runtime/blend.hpp"

#include <cmath>
#include <numbers>

namespace animgraph {
namespace {

RawJoint raw_joint(std::string name, std::optional<std::uint32_t> parent,
                   Vec3 translation, std::optional<std::string> semantic = std::nullopt) {
  return RawJoint{.name = std::move(name), .parent = parent,
                  .reference_local = Transform{translation, Quat::identity(), Vec3{1, 1, 1}},
                  .inverse_bind = Transform::identity(), .semantic = std::move(semantic)};
}

AnimationClip locomotion_clip(std::string name, float distance, float stride,
                              float turn_radians = 0.0F) {
  AnimationClip clip;
  clip.name = std::move(name);
  clip.duration = AnimTime{48'000};
  clip.mode = ClipPlaybackMode::loop;
  JointTrack root;
  root.joint = JointId{0};
  for (std::int64_t tick = 0; tick <= clip.duration.ticks; tick += 3'000) {
    const float phase = static_cast<float>(tick) / static_cast<float>(clip.duration.ticks);
    root.translations.push_back({AnimTime{tick}, Vec3{distance * phase, 0, 0}});
    root.rotations.push_back({AnimTime{tick}, *from_axis_angle(Vec3{0, 1, 0}, turn_radians * phase)});
    root.scales.push_back({AnimTime{tick}, Vec3{1, 1, 1}});
  }
  clip.tracks.push_back(std::move(root));
  for (const std::uint32_t joint : {1U, 6U, 9U, 12U}) {
    JointTrack limb;
    limb.joint = JointId{joint};
    for (std::int64_t tick = 0; tick <= clip.duration.ticks; tick += 6'000) {
      const float phase = static_cast<float>(tick) / static_cast<float>(clip.duration.ticks);
      const float angle = stride * std::sin(phase * 2.0F * std::numbers::pi_v<float> +
                                            (joint % 2U ? 0.0F : std::numbers::pi_v<float>));
      limb.rotations.push_back({AnimTime{tick}, *from_axis_angle(Vec3{0, 0, 1}, angle)});
    }
    clip.tracks.push_back(std::move(limb));
  }
  if (stride != 0.0F) {
    clip.events = {{AnimTime{12'000}, "foot_left", 1}, {AnimTime{36'000}, "foot_right", 2}};
    clip.markers = {{AnimTime{12'000}, "left_plant"}, {AnimTime{36'000}, "right_plant"}};
  }
  return clip;
}

AnimationClip pose_clip(std::string name, std::initializer_list<std::uint32_t> joints,
                        float radians) {
  AnimationClip clip;
  clip.name = std::move(name);
  clip.duration = AnimTime{48'000};
  clip.mode = ClipPlaybackMode::loop;
  for (const auto joint : joints) {
    JointTrack track;
    track.joint = JointId{joint};
    track.rotations = {{AnimTime{0}, *from_axis_angle(Vec3{0, 0, 1}, radians)},
                       {AnimTime{48'000}, *from_axis_angle(Vec3{0, 0, 1}, radians)}};
    clip.tracks.push_back(std::move(track));
  }
  return clip;
}

}  // namespace

Expected<CompiledSkeleton, Error> make_procedural_humanoid() {
  RawSkeleton raw{{
      raw_joint("Root", std::nullopt, {0, 0, 0}, "root"),
      raw_joint("LeftUpperLeg", 0, {-0.25F, -0.9F, 0}, "upper_leg_l"),
      raw_joint("LeftFoot", 1, {0, -0.9F, 0}, "foot_l"),
      raw_joint("Spine", 0, {0, 0.8F, 0}, "spine"),
      raw_joint("Chest", 3, {0, 0.7F, 0}, "chest"),
      raw_joint("Head", 4, {0, 0.7F, 0}, "head"),
      raw_joint("LeftUpperArm", 4, {-0.55F, 0.45F, 0}, "upper_arm_l"),
      raw_joint("LeftLowerArm", 6, {-0.65F, 0, 0}, "lower_arm_l"),
      raw_joint("LeftHand", 7, {-0.45F, 0, 0}, "hand_l"),
      raw_joint("RightUpperArm", 4, {0.55F, 0.45F, 0}, "upper_arm_r"),
      raw_joint("RightLowerArm", 9, {0.65F, 0, 0}, "lower_arm_r"),
      raw_joint("RightHand", 10, {0.45F, 0, 0}, "hand_r"),
      raw_joint("RightUpperLeg", 0, {0.25F, -0.9F, 0}, "upper_leg_r"),
      raw_joint("RightLowerLeg", 12, {0, -0.9F, 0}, "lower_leg_r"),
      raw_joint("RightFoot", 13, {0, -0.35F, 0.2F}, "foot_r")}};
  return compile_skeleton(raw);
}

Expected<DemoBundle, Error> make_locomotion_demo() {
  DemoBundle demo;
  auto skeleton = make_procedural_humanoid();
  if (!skeleton) return make_unexpected(skeleton.error());
  demo.skeleton = std::move(*skeleton);
  demo.clips = {
      locomotion_clip("Idle", 0.0F, 0.0F),
      locomotion_clip("Walk", 2.0F, 0.35F),
      locomotion_clip("Run", 4.0F, 0.7F),
      locomotion_clip("Turn", 0.5F, 0.25F, std::numbers::pi_v<float> / 2.0F),
      pose_clip("AimAdditive", {6, 7, 9, 10}, 0.35F),
      pose_clip("UpperBodyLayer", {3, 4, 6, 9}, -0.2F)};
  demo.clips[0].markers = {{AnimTime{12'000}, "left_plant"},
                           {AnimTime{36'000}, "right_plant"}};

  GraphBuilder builder;
  const auto reference = builder.add_node(NodeType::reference_pose, "ReferencePose");
  const auto idle = builder.add_node(NodeType::clip_player, "IdlePlayer");
  const auto walk = builder.add_node(NodeType::clip_player, "WalkPlayer");
  const auto run = builder.add_node(NodeType::clip_player, "RunPlayer");
  const auto turn = builder.add_node(NodeType::clip_player, "TurnPlayer");
  const auto aim = builder.add_node(NodeType::clip_player, "AimPlayer");
  const auto upper = builder.add_node(NodeType::clip_player, "UpperLayerPlayer");
  for (const auto& [node, clip] : {std::pair{idle, 0U}, std::pair{walk, 1U},
                                   std::pair{run, 2U}, std::pair{turn, 3U},
                                   std::pair{aim, 4U}, std::pair{upper, 5U}}) {
    auto bound = builder.set_clip(node, clip);
    if (!bound) return make_unexpected(bound.error());
    builder.configure_clip_player(node, JointId{0}, false).value();
  }
  const auto blend1d = builder.add_node(NodeType::blend_1d, "SpeedBlend1D");
  builder.configure_blend_1d(blend1d, {0.0F, 1.0F}, "p0_target_x", 0.5F).value();
  builder.connect(PosePin{reference, 0}, PosePin{blend1d, 0}).value();
  builder.connect(PosePin{walk, 0}, PosePin{blend1d, 1}).value();
  const auto blend2d = builder.add_node(NodeType::blend_2d, "LocomotionBlend2D");
  builder.configure_blend_2d(blend2d,
      {GraphPoint2{0,0}, GraphPoint2{1,0}, GraphPoint2{0,1}},
      {std::string{"p0_target_x"}, std::string{"p2_target_z"}}).value();
  builder.connect(PosePin{blend1d, 0}, PosePin{blend2d, 0}).value();
  builder.connect(PosePin{run, 0}, PosePin{blend2d, 1}).value();
  builder.connect(PosePin{turn, 0}, PosePin{blend2d, 2}).value();
  const auto state = builder.add_node(NodeType::state_machine, "LocomotionStateMachine");
  StateMachineNodeConfig state_config;
  state_config.definition.states = {{StateId{0}, "Idle", AnimTime{48'000}},
                                    {StateId{1}, "Locomotion", AnimTime{48'000}}};
  state_config.definition.entry = StateId{0};
  state_config.definition.transitions = {
      TransitionDefinition{StateId{0},StateId{1},ParameterCondition{0,CompareOp::greater_equal,0.5F},
                           1,AnimTime{12'000},std::nullopt,std::string{"left_plant"},true},
      TransitionDefinition{StateId{1},StateId{0},ParameterCondition{0,CompareOp::less,0.5F},
                           1,AnimTime{12'000},std::nullopt,std::string{"left_plant"},true}};
  state_config.sync_clip_indices = {0U, 1U};
  builder.configure_state_machine(state, std::move(state_config)).value();
  builder.connect(PosePin{idle, 0}, PosePin{state, 0}).value();
  builder.connect(PosePin{blend2d, 0}, PosePin{state, 1}).value();
  const auto additive = builder.add_node(NodeType::additive, "AimAdditive");
  builder.configure_additive(additive, "p3_ik_weight", 1.0F).value();
  builder.connect(PosePin{state, 0}, PosePin{additive, 0}).value();
  builder.connect(PosePin{aim, 0}, PosePin{additive, 1}).value();
  const auto layer = builder.add_node(NodeType::layered_blend_per_bone, "UpperBodyLayer");
  const auto upper_mask = make_hierarchy_mask(demo.skeleton,
      std::array{LayerBranch{JointId{3}, 1.0F}}).value();
  builder.configure_layered(layer, upper_mask, "p3_ik_weight", 1.0F).value();
  builder.connect(PosePin{additive, 0}, PosePin{layer, 0}).value();
  builder.connect(PosePin{upper, 0}, PosePin{layer, 1}).value();
  const auto cache = builder.add_node(NodeType::pose_cache, "FinalPoseCache");
  builder.connect(PosePin{layer, 0}, PosePin{cache, 0}).value();
  const auto ik = builder.add_node(NodeType::two_bone_ik, "LeftFootIK");
  TwoBoneIkNodeConfig ik_config;
  ik_config.root = JointId{0}; ik_config.mid = JointId{1}; ik_config.end = JointId{2};
  ik_config.pole = Vec3{0,0,1};
  ik_config.parameters = {"p0_target_x","p1_target_y","p2_target_z","p3_ik_weight"};
  ik_config.limit = JointLimit{0.05F, 3.10F};
  builder.configure_two_bone_ik(ik, std::move(ik_config)).value();
  builder.connect(PosePin{cache, 0}, PosePin{ik, 0}).value();
  const auto hand_ik = builder.add_node(NodeType::two_bone_ik, "LeftHandIK");
  TwoBoneIkNodeConfig hand_config;
  hand_config.root = JointId{6}; hand_config.mid = JointId{7}; hand_config.end = JointId{8};
  hand_config.pole = Vec3{0,0,1};
  hand_config.parameters = {"p4_hand_x","p5_hand_y","p6_hand_z","p7_hand_weight"};
  hand_config.limit = JointLimit{0.05F, 3.10F};
  builder.configure_two_bone_ik(hand_ik, std::move(hand_config)).value();
  builder.connect(PosePin{ik, 0}, PosePin{hand_ik, 0}).value();
  const auto output = builder.add_node(NodeType::output, "Output");
  builder.connect(PosePin{hand_ik, 0}, PosePin{output, 0}).value();
  builder.set_output(output);
  builder.add_parameter(GraphParameter{"p0_target_x", 0.5F, false});
  builder.add_parameter(GraphParameter{"p1_target_y", -1.5F, false});
  builder.add_parameter(GraphParameter{"p2_target_z", 0.0F, false});
  builder.add_parameter(GraphParameter{"p3_ik_weight", 1.0F, false});
  builder.add_parameter(GraphParameter{"p4_hand_x", -1.4F, false});
  builder.add_parameter(GraphParameter{"p5_hand_y", 2.0F, false});
  builder.add_parameter(GraphParameter{"p6_hand_z", 0.2F, false});
  builder.add_parameter(GraphParameter{"p7_hand_weight", 0.75F, false});
  demo.graph_description = builder.build();
  auto graph = compile_graph(demo.graph_description);
  if (!graph) return make_unexpected(graph.error());
  demo.graph = std::move(*graph);
  CompressionReport compression;
  auto compressed = compress_clip(demo.clips[1], CompressionSettings{0.02F, 0.01F, 0.01F}, compression);
  if (!compressed) return make_unexpected(compressed.error());
  demo.compression = std::move(compression);
  return demo;
}

}  // namespace animgraph
