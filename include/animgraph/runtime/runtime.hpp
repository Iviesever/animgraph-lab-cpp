#pragma once

#include "animgraph/clip/clip.hpp"
#include "animgraph/core/expected.hpp"
#include "animgraph/graph/graph.hpp"
#include "animgraph/ik/two_bone_ik.hpp"
#include "animgraph/skeleton/skeleton.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace animgraph {

struct EvaluationContext {
  const CompiledSkeleton& skeleton;
  std::span<const AnimationClip> clips;
  AnimTime delta;
  std::span<const float> parameters;
  std::uint64_t frame{};
  std::uint64_t generation{};
};

struct RuntimeEventOccurrence {
  AnimationEvent event;
  AnimTime absolute_time;
  std::int64_t cycle{};
  NodeId source_node;
  std::size_t clip_index{};
};

struct BlendObservation {
  NodeId node;
  std::string name;
  std::vector<float> weights;
};

struct IkObservation {
  NodeId node;
  std::string name;
  JointId root;
  JointId mid;
  JointId end;
  Vec3 target;
  Vec3 pole;
  float error{};
  IkStatus status{IkStatus::degenerate};
};

struct EvaluationResult {
  LocalPose pose;
  std::vector<AnimationEvent> events;
  std::vector<RuntimeEventOccurrence> event_occurrences;
  Transform root_motion{Transform::identity()};
  Transform root_accumulated{Transform::identity()};
  std::uint32_t pose_cache_hits{};
  std::uint32_t pose_cache_misses{};
  std::string current_node;
  std::vector<std::string> executed_nodes;
  std::string state;
  float transition_progress{};
  std::vector<BlendObservation> blends;
  std::vector<std::string> sync_markers;
  bool ik_applied{};
  Vec3 ik_target{};
  Vec3 ik_pole{};
  float ik_error{};
  std::vector<IkObservation> ik_nodes;
};

struct PoseCacheState {
  bool valid{};
  std::uint64_t frame{};
  std::uint64_t generation{};
  std::uint64_t parameter_hash{};
  LocalPose pose;
  Transform root_motion{Transform::identity()};
  std::vector<RuntimeEventOccurrence> events;
};

struct RuntimeStateNode {
  bool target_state{};
  bool transitioning{};
  AnimTime elapsed;
};

struct GraphInstance {
  std::vector<AnimTime> clip_times;
  std::vector<LocalPose> pose_slots;
  std::vector<Transform> root_motion_slots;
  std::vector<std::vector<RuntimeEventOccurrence>> event_slots;
  std::vector<std::uint8_t> initialized;
  std::vector<PoseCacheState> pose_caches;
  std::vector<RuntimeStateNode> state_nodes;
  std::vector<std::optional<StateMachineInstance>> state_machines;
  std::vector<std::byte> state;
  Transform root_motion_accumulator{Transform::identity()};
  std::optional<EvaluationResult> memo;
  std::uint64_t memo_frame{};
  std::uint64_t memo_generation{};
  std::uint64_t memo_parameter_hash{};
  std::size_t joint_count{};
};

[[nodiscard]] Expected<GraphInstance, Error> make_graph_instance(
    const CompiledGraph& graph, std::size_t joint_count);
[[nodiscard]] Expected<void, Error> validate_instance_layout(
    const CompiledGraph& graph, const GraphInstance& instance,
    std::size_t joint_count);
[[nodiscard]] Expected<EvaluationResult, Error> evaluate(
    const EvaluationContext& context, const CompiledGraph& graph,
    GraphInstance& instance);

}  // namespace animgraph
