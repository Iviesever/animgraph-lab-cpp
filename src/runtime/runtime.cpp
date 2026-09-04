#include "animgraph/runtime/runtime.hpp"

#include "animgraph/runtime/blend.hpp"
#include "animgraph/runtime/root_motion.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <tuple>

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
      instance.root_motion_slots.size() != graph.pose_slot_count ||
      instance.clip_times.size() != graph.instructions.size() ||
      instance.pose_caches.size() != graph.instructions.size() ||
      instance.state_nodes.size() != graph.instructions.size() ||
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
  instance.root_motion_slots.resize(graph.pose_slot_count, Transform::identity());
  instance.initialized.resize(graph.pose_slot_count);
  instance.pose_caches.resize(graph.instructions.size());
  instance.state_nodes.resize(graph.instructions.size());
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
  const auto parameter = [&context](std::size_t index, float fallback) {
    return index < context.parameters.size() ? context.parameters[index] : fallback;
  };

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
    Transform root_output = Transform::identity();
    switch (instruction.type) {
      case NodeType::reference_pose:
        for (std::size_t joint = 0; joint < context.skeleton.joints.size(); ++joint) {
          output.transforms[joint] = context.skeleton.joints[joint].reference_local;
        }
        root_output = Transform::identity();
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
        if (!context.skeleton.joints.empty()) {
          const auto motion = extract_root_motion(context.skeleton,
              context.clips[*instruction.clip_index], JointId{0}, previous,
              instance.clip_times[index]);
          if (!motion) return make_unexpected(motion.error());
          root_output = *motion;
        }
        auto events = query_events(context.clips[*instruction.clip_index], previous,
                                   instance.clip_times[index]);
        result.events.insert(result.events.end(), events.begin(), events.end());
        break;
      }
      case NodeType::blend_1d: {
        const float weight = std::clamp(parameter(0, 0.5F), 0.0F, 1.0F);
        const std::array samples{
            Blend1DSample{0.0F, instance.pose_slots[instruction.inputs[0].value]},
            Blend1DSample{1.0F, instance.pose_slots[instruction.inputs[1].value]}};
        const auto blended = evaluate_blend_1d(samples, weight);
        if (!blended) return make_unexpected(blended.error());
        copy_pose(output, *blended);
        root_output = blend_root_motion(instance.root_motion_slots[instruction.inputs[0].value],
                                        instance.root_motion_slots[instruction.inputs[1].value], weight);
        break;
      }
      case NodeType::blend_2d: {
        const std::array samples{
            Blend2DSample{Vec2{0, 0}, instance.pose_slots[instruction.inputs[0].value]},
            Blend2DSample{Vec2{1, 0}, instance.pose_slots[instruction.inputs[1].value]},
            Blend2DSample{Vec2{0, 1}, instance.pose_slots[instruction.inputs[2].value]}};
        const auto blended = evaluate_blend_2d(samples, Vec2{parameter(0, 0.0F), parameter(1, 0.0F)});
        if (!blended) return make_unexpected(blended.error());
        copy_pose(output, blended->pose);
        const Transform first = blend_root_motion(
            instance.root_motion_slots[instruction.inputs[0].value],
            instance.root_motion_slots[instruction.inputs[1].value],
            blended->weights[1] / std::max(1.0e-8F, blended->weights[0] + blended->weights[1]));
        root_output = blend_root_motion(first,
            instance.root_motion_slots[instruction.inputs[2].value], blended->weights[2]);
        break;
      }
      case NodeType::additive: {
        LocalPose reference;
        reference.transforms.reserve(context.skeleton.joints.size());
        for (const auto& joint : context.skeleton.joints) reference.transforms.push_back(joint.reference_local);
        const auto added = additive_pose(instance.pose_slots[instruction.inputs[0].value],
                                         instance.pose_slots[instruction.inputs[1].value], reference,
                                         std::clamp(parameter(0, 1.0F), 0.0F, 1.0F));
        if (!added) return make_unexpected(added.error());
        copy_pose(output, *added);
        root_output = instance.root_motion_slots[instruction.inputs[0].value];
        break;
      }
      case NodeType::layered_blend_per_bone: {
        const float weight = std::clamp(parameter(0, 1.0F), 0.0F, 1.0F);
        const std::vector<float> weights(context.skeleton.joints.size(), weight);
        const auto layered = layered_blend(instance.pose_slots[instruction.inputs[0].value],
                                           instance.pose_slots[instruction.inputs[1].value], weights);
        if (!layered) return make_unexpected(layered.error());
        copy_pose(output, *layered);
        root_output = blend_root_motion(instance.root_motion_slots[instruction.inputs[0].value],
                                        instance.root_motion_slots[instruction.inputs[1].value], weight);
        break;
      }
      case NodeType::two_bone_ik: {
        if (context.skeleton.joints.size() < 3) {
          return make_unexpected(Error{ErrorCode::bounds, "TwoBoneIK requires at least three joints"});
        }
        copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
        const Vec3 target{parameter(0, 0.0F), parameter(1, 0.0F), parameter(2, 0.0F)};
        const auto solved = solve_two_bone_ik(context.skeleton, output,
            TwoBoneIkRequest{JointId{0}, JointId{1}, JointId{2}, target,
                             Vec3{0, 0, 1}, std::clamp(parameter(3, 1.0F), 0.0F, 1.0F),
                             std::nullopt});
        if (!solved) return make_unexpected(solved.error());
        result.ik_applied = true;
        result.ik_target = target;
        result.ik_error = solved->target_error;
        root_output = instance.root_motion_slots[instruction.inputs[0].value];
        break;
      }
      case NodeType::pose_cache: {
        auto& cache = instance.pose_caches[index];
        if (cache.valid && cache.frame == context.frame && cache.generation == context.generation &&
            cache.parameter_hash == parameters) {
          copy_pose(output, cache.pose);
          root_output = cache.root_motion;
          ++result.pose_cache_hits;
        } else {
          copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
          copy_pose(cache.pose, output);
          root_output = instance.root_motion_slots[instruction.inputs[0].value];
          cache.root_motion = root_output;
          cache.valid = true;
          cache.frame = context.frame;
          cache.generation = context.generation;
          cache.parameter_hash = parameters;
          ++result.pose_cache_misses;
        }
        break;
      }
      case NodeType::state_machine: {
        auto& state = instance.state_nodes[index];
        const bool desired = parameter(0, 0.0F) >= 0.5F;
        if (desired != state.target_state) {
          state.target_state = desired;
          state.transitioning = true;
          state.elapsed = AnimTime{0};
        }
        float weight = state.target_state ? 1.0F : 0.0F;
        if (state.transitioning) {
          state.elapsed.ticks += std::max<std::int64_t>(0, context.delta.ticks);
          const float progress = std::clamp(static_cast<float>(state.elapsed.ticks) / 12'000.0F,
                                            0.0F, 1.0F);
          weight = state.target_state ? progress : 1.0F - progress;
          if (progress >= 1.0F) state.transitioning = false;
        }
        const auto blended = blend_poses(instance.pose_slots[instruction.inputs[0].value],
                                         instance.pose_slots[instruction.inputs[1].value], weight);
        if (!blended) return make_unexpected(blended.error());
        copy_pose(output, *blended);
        root_output = blend_root_motion(instance.root_motion_slots[instruction.inputs[0].value],
                                        instance.root_motion_slots[instruction.inputs[1].value], weight);
        break;
      }
      case NodeType::output:
        copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
        root_output = instance.root_motion_slots[instruction.inputs[0].value];
        break;
      default:
        return make_unexpected(Error{ErrorCode::unsupported, "node execution is not implemented in this PACT"});
    }
    instance.root_motion_slots[instruction.output.value] = root_output;
    instance.initialized[instruction.output.value] = 1;
  }
  copy_pose(result.pose, instance.pose_slots[graph.output_slot.value]);
  result.root_motion = instance.root_motion_slots[graph.output_slot.value];
  std::vector<AnimationEvent> unique_events;
  unique_events.reserve(result.events.size());
  for (const auto& event : result.events) {
    const auto duplicate = std::ranges::find_if(unique_events, [&event](const AnimationEvent& existing) {
      return std::tie(existing.time.ticks, existing.name, existing.payload) ==
             std::tie(event.time.ticks, event.name, event.payload);
    });
    if (duplicate == unique_events.end()) unique_events.push_back(event);
  }
  result.events = std::move(unique_events);
  return result;
}

}  // namespace animgraph
