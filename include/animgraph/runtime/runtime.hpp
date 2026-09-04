#pragma once

#include "animgraph/clip/clip.hpp"
#include "animgraph/core/expected.hpp"
#include "animgraph/graph/graph.hpp"
#include "animgraph/skeleton/skeleton.hpp"

#include <cstdint>
#include <span>
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

struct PoseCacheState {
  bool valid{};
  std::uint64_t frame{};
  std::uint64_t generation{};
  std::uint64_t parameter_hash{};
  LocalPose pose;
  Transform root_motion{Transform::identity()};
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
  std::vector<std::uint8_t> initialized;
  std::vector<PoseCacheState> pose_caches;
  std::vector<RuntimeStateNode> state_nodes;
  std::vector<std::byte> state;
  std::size_t joint_count{};
};

struct EvaluationResult {
  LocalPose pose;
  std::vector<AnimationEvent> events;
  Transform root_motion{Transform::identity()};
  std::uint32_t pose_cache_hits{};
  std::uint32_t pose_cache_misses{};
};

[[nodiscard]] Expected<GraphInstance, Error> make_graph_instance(
    const CompiledGraph& graph, std::size_t joint_count);
[[nodiscard]] Expected<EvaluationResult, Error> evaluate(
    const EvaluationContext& context, const CompiledGraph& graph,
    GraphInstance& instance);

}  // namespace animgraph
