#include "animgraph/runtime/runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace animgraph {
namespace {

std::uint64_t parameter_hash(std::span<const float> parameters) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const float value : parameters) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned byte = 0; byte < 4; ++byte) {
      hash ^= (bits >> (byte * 8U)) & 0xFFU;
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

void copy_pose(LocalPose& destination, const LocalPose& source) {
  destination.transforms = source.transforms;
}

Expected<void, Error> validate_instance(const CompiledGraph& graph,
                                        const GraphInstance& instance) {
  if (instance.pose_slots.size() != graph.pose_slot_count ||
      instance.initialized.size() != graph.pose_slot_count ||
      instance.clip_times.size() != graph.instructions.size() ||
      instance.pose_caches.size() != graph.instructions.size() ||
      instance.state.size() != graph.state_size) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "graph instance layout mismatch"});
  }
  return {};
}

}  // namespace

Expected<GraphInstance, Error> make_graph_instance(const CompiledGraph& graph,
                                                   std::size_t joint_count) {
  if (joint_count == 0 || joint_count > max_joints || graph.pose_slot_count == 0) {
    return make_unexpected(Error{ErrorCode::bounds, "graph instance dimensions are invalid"});
  }
  GraphInstance instance;
  instance.joint_count = joint_count;
  instance.clip_times.resize(graph.instructions.size());
  instance.pose_slots.resize(graph.pose_slot_count);
  instance.initialized.resize(graph.pose_slot_count);
  instance.pose_caches.resize(graph.instructions.size());
  instance.state.resize(graph.state_size);
  for (auto& pose : instance.pose_slots) pose.transforms.resize(joint_count);
  for (auto& cache : instance.pose_caches) cache.pose.transforms.resize(joint_count);
  return instance;
}

Expected<EvaluationResult, Error> evaluate(const EvaluationContext& context,
                                           const CompiledGraph& graph,
                                           GraphInstance& instance) {
  const auto instance_valid = validate_instance(graph, instance);
  if (!instance_valid) return make_unexpected(instance_valid.error());
  if (context.skeleton.joints.size() != instance.joint_count ||
      context.parameters.size() != graph.parameters.size()) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "evaluation context layout mismatch"});
  }
  for (const float value : context.parameters) {
    if (!std::isfinite(value)) return make_unexpected(Error{ErrorCode::non_finite, "parameter is not finite"});
  }
  std::ranges::fill(instance.initialized, std::uint8_t{0});
  EvaluationResult result;
  result.events.reserve(max_events);
  const std::uint64_t parameters = parameter_hash(context.parameters);

  for (std::size_t index = 0; index < graph.instructions.size(); ++index) {
    const auto& instruction = graph.instructions[index];
    if (instruction.output.value >= instance.pose_slots.size()) {
      return make_unexpected(Error{ErrorCode::graph, "instruction output slot is invalid"});
    }
    for (const auto input : instruction.inputs) {
      if (input.value >= instance.pose_slots.size() || !instance.initialized[input.value]) {
        return make_unexpected(Error{ErrorCode::graph, "instruction reads uninitialized pose slot"});
      }
    }
    auto& output = instance.pose_slots[instruction.output.value];
    switch (instruction.type) {
      case NodeType::reference_pose:
        for (std::size_t joint = 0; joint < context.skeleton.joints.size(); ++joint) {
          output.transforms[joint] = context.skeleton.joints[joint].reference_local;
        }
        break;
      case NodeType::clip_player: {
        if (!instruction.clip_index || *instruction.clip_index >= context.clips.size()) {
          return make_unexpected(Error{ErrorCode::bounds, "clip player index is invalid"});
        }
        const auto previous = instance.clip_times[index];
        if ((context.delta.ticks > 0 && previous.ticks > std::numeric_limits<std::int64_t>::max() - context.delta.ticks) ||
            (context.delta.ticks < 0 && previous.ticks < std::numeric_limits<std::int64_t>::min() - context.delta.ticks)) {
          return make_unexpected(Error{ErrorCode::bounds, "clip time overflow"});
        }
        instance.clip_times[index].ticks += context.delta.ticks;
        const auto sampled = sample_clip(context.skeleton, context.clips[*instruction.clip_index],
                                         instance.clip_times[index]);
        if (!sampled) return make_unexpected(sampled.error());
        copy_pose(output, sampled->pose);
        auto events = query_events(context.clips[*instruction.clip_index], previous,
                                   instance.clip_times[index]);
        result.events.insert(result.events.end(), events.begin(), events.end());
        break;
      }
      case NodeType::pose_cache: {
        auto& cache = instance.pose_caches[index];
        if (cache.valid && cache.frame == context.frame && cache.generation == context.generation &&
            cache.parameter_hash == parameters) {
          copy_pose(output, cache.pose);
          ++result.pose_cache_hits;
        } else {
          copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
          copy_pose(cache.pose, output);
          cache.valid = true;
          cache.frame = context.frame;
          cache.generation = context.generation;
          cache.parameter_hash = parameters;
          ++result.pose_cache_misses;
        }
        break;
      }
      case NodeType::output:
        copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
        break;
      default:
        return make_unexpected(Error{ErrorCode::unsupported, "node execution is not implemented in this PACT"});
    }
    instance.initialized[instruction.output.value] = 1;
  }
  copy_pose(result.pose, instance.pose_slots[graph.output_slot.value]);
  return result;
}

}  // namespace animgraph
