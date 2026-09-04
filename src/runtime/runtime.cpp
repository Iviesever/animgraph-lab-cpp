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

void append_events(std::vector<RuntimeEventOccurrence>& destination,
                   const std::vector<RuntimeEventOccurrence>& source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

}  // namespace

Expected<void, Error> validate_instance_layout(const CompiledGraph& graph,
                                               const GraphInstance& instance,
                                               std::size_t joint_count) {
  if (instance.pose_slots.size() != graph.pose_slot_count ||
      instance.initialized.size() != graph.pose_slot_count ||
      instance.root_motion_slots.size() != graph.pose_slot_count ||
      instance.event_slots.size() != graph.pose_slot_count ||
      instance.clip_times.size() != graph.instructions.size() ||
      instance.pose_caches.size() != graph.instructions.size() ||
      instance.state_nodes.size() != graph.instructions.size() ||
      instance.state_machines.size() != graph.instructions.size() ||
      instance.state.size() != graph.state_size || instance.joint_count != joint_count ||
      graph.output_slot.value >= graph.pose_slot_count) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "graph instance layout mismatch"});
  }
  for (const auto& pose : instance.pose_slots) {
    if (pose.transforms.size() != joint_count)
      return make_unexpected(Error{ErrorCode::size_mismatch, "pose slot joint count mismatch"});
  }
  for (const auto& cache : instance.pose_caches) {
    if (cache.pose.transforms.size() != joint_count)
      return make_unexpected(Error{ErrorCode::size_mismatch, "pose cache joint count mismatch"});
  }
  for (const auto& instruction : graph.instructions) {
    if (instruction.output.value >= graph.pose_slot_count)
      return make_unexpected(Error{ErrorCode::graph, "compiled output slot is invalid"});
    for (const auto input : instruction.inputs)
      if (input.value >= graph.pose_slot_count)
        return make_unexpected(Error{ErrorCode::graph, "compiled input slot is invalid"});
  }
  return {};
}

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
  instance.event_slots.resize(graph.pose_slot_count);
  instance.initialized.resize(graph.pose_slot_count);
  instance.pose_caches.resize(graph.instructions.size());
  instance.state_nodes.resize(graph.instructions.size());
  instance.state_machines.resize(graph.instructions.size());
  instance.state.resize(graph.state_size);
  for (auto& pose : instance.pose_slots) pose.transforms.resize(joint_count);
  for (auto& cache : instance.pose_caches) cache.pose.transforms.resize(joint_count);
  for (auto& events : instance.event_slots) events.reserve(max_events);
  for (std::size_t index = 0; index < graph.instructions.size(); ++index) {
    if (const auto* config = std::get_if<StateMachineNodeConfig>(&graph.instructions[index].config)) {
      auto state = make_state_machine_instance(config->definition);
      if (!state) return make_unexpected(state.error());
      instance.state_machines[index] = std::move(*state);
    }
  }
  return instance;
}

Expected<EvaluationResult, Error> evaluate(const EvaluationContext& context,
                                           const CompiledGraph& graph,
                                           GraphInstance& instance) {
  const auto instance_valid = validate_instance_layout(graph, instance,
                                                       context.skeleton.joints.size());
  if (!instance_valid) return make_unexpected(instance_valid.error());
  if (context.skeleton.joints.size() != instance.joint_count ||
      context.parameters.size() != graph.parameters.size()) {
    return make_unexpected(Error{ErrorCode::size_mismatch, "evaluation context layout mismatch"});
  }
  for (const float value : context.parameters) {
    if (!std::isfinite(value)) return make_unexpected(Error{ErrorCode::non_finite, "parameter is not finite"});
  }
  const std::uint64_t parameters = parameter_hash(context.parameters);
  const auto cache_count = static_cast<std::uint32_t>(std::ranges::count(
      graph.instructions, NodeType::pose_cache, &CompiledInstruction::type));
  if (cache_count > 0 && instance.memo && instance.memo_frame == context.frame &&
      instance.memo_generation == context.generation && instance.memo_parameter_hash == parameters) {
    EvaluationResult cached = *instance.memo;
    cached.pose_cache_hits = cache_count;
    cached.pose_cache_misses = 0;
    return cached;
  }
  std::ranges::fill(instance.initialized, std::uint8_t{0});
  for (auto& events : instance.event_slots) events.clear();
  EvaluationResult result;
  result.events.reserve(max_events);
  result.event_occurrences.reserve(max_events);
  const auto parameter = [&context](const CompiledInstruction& instruction,
                                    std::size_t binding, float fallback) {
    if (binding >= instruction.parameter_indices.size()) return fallback;
    const auto index = instruction.parameter_indices[binding];
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
    auto& output_events = instance.event_slots[instruction.output.value];
    output_events.clear();
    Transform root_output = Transform::identity();
    result.current_node = instruction.name;
    result.executed_nodes.push_back(instruction.name);
    switch (instruction.type) {
      case NodeType::reference_pose:
        for (std::size_t joint = 0; joint < context.skeleton.joints.size(); ++joint) {
          output.transforms[joint] = context.skeleton.joints[joint].reference_local;
        }
        root_output = Transform::identity();
        break;
      case NodeType::clip_player: {
        const auto* config = std::get_if<ClipPlayerNodeConfig>(&instruction.config);
        if (!config) return make_unexpected(Error{ErrorCode::graph, "ClipPlayer config is missing"});
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
              context.clips[*instruction.clip_index], config->root_motion_joint, previous,
              instance.clip_times[index]);
          if (!motion) return make_unexpected(motion.error());
          root_output = *motion;
        }
        if (config->remove_root_motion)
          apply_root_motion_policy(output, config->root_motion_joint,
                                   RootMotionPosePolicy::remove);
        const auto occurrences = query_event_occurrences(
            context.clips[*instruction.clip_index], previous, instance.clip_times[index]);
        if (!occurrences) return make_unexpected(occurrences.error());
        for (const auto& occurrence : *occurrences) {
          output_events.push_back(RuntimeEventOccurrence{occurrence.event,
              occurrence.absolute_time, occurrence.cycle, instruction.node,
              *instruction.clip_index});
        }
        AnimationClip marker_clip = context.clips[*instruction.clip_index];
        marker_clip.events.clear();
        for (const auto& marker : marker_clip.markers)
          marker_clip.events.push_back(AnimationEvent{marker.time, marker.name, 0});
        const auto marker_occurrences = query_event_occurrences(
            marker_clip, previous, instance.clip_times[index]);
        if (marker_occurrences) {
          for (const auto& occurrence : *marker_occurrences)
            result.sync_markers.push_back(occurrence.event.name);
        }
        break;
      }
      case NodeType::blend_1d: {
        const auto* config = std::get_if<Blend1DNodeConfig>(&instruction.config);
        if (!config) return make_unexpected(Error{ErrorCode::graph, "Blend1D config is missing"});
        const float value = parameter(instruction, 0, config->fallback);
        const std::array samples{
            Blend1DSample{config->thresholds[0], instance.pose_slots[instruction.inputs[0].value]},
            Blend1DSample{config->thresholds[1], instance.pose_slots[instruction.inputs[1].value]}};
        const auto blended = evaluate_blend_1d(samples, value);
        if (!blended) return make_unexpected(blended.error());
        const float weight = std::clamp((value - config->thresholds[0]) /
            (config->thresholds[1] - config->thresholds[0]), 0.0F, 1.0F);
        copy_pose(output, *blended);
        if (weight < 1.0F) append_events(output_events,
            instance.event_slots[instruction.inputs[0].value]);
        if (weight > 0.0F) append_events(output_events,
            instance.event_slots[instruction.inputs[1].value]);
        result.blends.push_back(BlendObservation{instruction.node, instruction.name,
                                                 {1.0F - weight, weight}});
        root_output = blend_root_motion(instance.root_motion_slots[instruction.inputs[0].value],
                                        instance.root_motion_slots[instruction.inputs[1].value], weight);
        break;
      }
      case NodeType::blend_2d: {
        const auto* config = std::get_if<Blend2DNodeConfig>(&instruction.config);
        if (!config) return make_unexpected(Error{ErrorCode::graph, "Blend2D config is missing"});
        const std::array samples{
            Blend2DSample{Vec2{config->points[0].x, config->points[0].y}, instance.pose_slots[instruction.inputs[0].value]},
            Blend2DSample{Vec2{config->points[1].x, config->points[1].y}, instance.pose_slots[instruction.inputs[1].value]},
            Blend2DSample{Vec2{config->points[2].x, config->points[2].y}, instance.pose_slots[instruction.inputs[2].value]}};
        const auto blended = evaluate_blend_2d(samples, Vec2{
            parameter(instruction, 0, config->fallbacks[0]),
            parameter(instruction, 1, config->fallbacks[1])});
        if (!blended) return make_unexpected(blended.error());
        copy_pose(output, blended->pose);
        for (std::size_t input = 0; input < 3; ++input)
          if (blended->weights[input] > 0.0F)
            append_events(output_events, instance.event_slots[instruction.inputs[input].value]);
        result.blends.push_back(BlendObservation{instruction.node, instruction.name,
            {blended->weights[0], blended->weights[1], blended->weights[2]}});
        const Transform first = blend_root_motion(
            instance.root_motion_slots[instruction.inputs[0].value],
            instance.root_motion_slots[instruction.inputs[1].value],
            blended->weights[1] / std::max(1.0e-8F, blended->weights[0] + blended->weights[1]));
        root_output = blend_root_motion(first,
            instance.root_motion_slots[instruction.inputs[2].value], blended->weights[2]);
        break;
      }
      case NodeType::additive: {
        const auto* config = std::get_if<AdditiveNodeConfig>(&instruction.config);
        if (!config) return make_unexpected(Error{ErrorCode::graph, "Additive config is missing"});
        LocalPose reference;
        reference.transforms.reserve(context.skeleton.joints.size());
        for (const auto& joint : context.skeleton.joints) reference.transforms.push_back(joint.reference_local);
        const float weight = std::clamp(parameter(instruction, 0, config->fallback), 0.0F, 1.0F);
        const auto added = additive_pose(instance.pose_slots[instruction.inputs[0].value],
                                         instance.pose_slots[instruction.inputs[1].value], reference,
                                         weight);
        if (!added) return make_unexpected(added.error());
        copy_pose(output, *added);
        append_events(output_events, instance.event_slots[instruction.inputs[0].value]);
        if (weight > 0.0F)
          append_events(output_events, instance.event_slots[instruction.inputs[1].value]);
        result.blends.push_back(BlendObservation{instruction.node, instruction.name,
                                                 {1.0F, weight}});
        root_output = instance.root_motion_slots[instruction.inputs[0].value];
        break;
      }
      case NodeType::layered_blend_per_bone: {
        const auto* config = std::get_if<LayeredNodeConfig>(&instruction.config);
        if (!config) return make_unexpected(Error{ErrorCode::graph, "Layer config is missing"});
        const float weight = std::clamp(parameter(instruction, 0, config->fallback), 0.0F, 1.0F);
        std::vector<float> weights(context.skeleton.joints.size(), weight);
        if (!config->joint_weights.empty()) {
          if (config->joint_weights.size() != weights.size())
            return make_unexpected(Error{ErrorCode::size_mismatch, "layer mask does not match skeleton"});
          for (std::size_t joint = 0; joint < weights.size(); ++joint)
            weights[joint] = config->joint_weights[joint] * weight;
        }
        const auto layered = layered_blend(instance.pose_slots[instruction.inputs[0].value],
                                           instance.pose_slots[instruction.inputs[1].value], weights);
        if (!layered) return make_unexpected(layered.error());
        copy_pose(output, *layered);
        append_events(output_events, instance.event_slots[instruction.inputs[0].value]);
        if (weight > 0.0F)
          append_events(output_events, instance.event_slots[instruction.inputs[1].value]);
        result.blends.push_back(BlendObservation{instruction.node, instruction.name,
                                                 {1.0F - weight, weight}});
        root_output = blend_root_motion(instance.root_motion_slots[instruction.inputs[0].value],
                                        instance.root_motion_slots[instruction.inputs[1].value], weights.front());
        break;
      }
      case NodeType::two_bone_ik: {
        const auto* config = std::get_if<TwoBoneIkNodeConfig>(&instruction.config);
        if (!config) return make_unexpected(Error{ErrorCode::graph, "TwoBoneIK config is missing"});
        copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
        append_events(output_events, instance.event_slots[instruction.inputs[0].value]);
        const Vec3 target{parameter(instruction, 0, config->fallbacks[0]),
                          parameter(instruction, 1, config->fallbacks[1]),
                          parameter(instruction, 2, config->fallbacks[2])};
        const auto solved = solve_two_bone_ik(context.skeleton, output,
            TwoBoneIkRequest{config->root, config->mid, config->end, target,
                             config->pole, std::clamp(parameter(instruction, 3, config->fallbacks[3]), 0.0F, 1.0F),
                             config->limit});
        if (!solved) return make_unexpected(solved.error());
        result.ik_applied = true;
        result.ik_target = target;
        result.ik_pole = config->pole;
        result.ik_error = solved->target_error;
        result.ik_nodes.push_back(IkObservation{instruction.node, instruction.name,
            config->root, config->mid, config->end, target, config->pole,
            solved->target_error, solved->status});
        root_output = instance.root_motion_slots[instruction.inputs[0].value];
        break;
      }
      case NodeType::pose_cache: {
        auto& cache = instance.pose_caches[index];
        if (cache.valid && cache.frame == context.frame && cache.generation == context.generation &&
            cache.parameter_hash == parameters) {
          copy_pose(output, cache.pose);
          output_events = cache.events;
          root_output = cache.root_motion;
          ++result.pose_cache_hits;
        } else {
          copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
          copy_pose(cache.pose, output);
          append_events(output_events, instance.event_slots[instruction.inputs[0].value]);
          cache.events = output_events;
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
        const auto* config = std::get_if<StateMachineNodeConfig>(&instruction.config);
        if (!config || !instance.state_machines[index])
          return make_unexpected(Error{ErrorCode::graph, "StateMachine config or instance is missing"});
        const auto update = update_state_machine(config->definition, context.parameters,
                                                 context.delta, *instance.state_machines[index]);
        if (!update) return make_unexpected(update.error());
        if (!update->events.empty()) {
          const auto transition = std::ranges::find_if(config->definition.transitions,
              [&update](const TransitionDefinition& item) {
                return item.source == update->source && item.target == update->target;
              });
          const auto source_state_position = std::ranges::find(
              config->definition.states, update->source, &StateDefinition::id) -
              config->definition.states.begin();
          const auto target_state_position = std::ranges::find(
              config->definition.states, update->target, &StateDefinition::id) -
              config->definition.states.begin();
          if (transition != config->definition.transitions.end() && transition->sync_marker &&
              source_state_position < 2 && target_state_position < 2 &&
              config->sync_clip_indices[source_state_position] &&
              config->sync_clip_indices[target_state_position]) {
            const auto source_clip = *config->sync_clip_indices[source_state_position];
            const auto target_clip = *config->sync_clip_indices[target_state_position];
            if (source_clip < context.clips.size() && target_clip < context.clips.size()) {
              const auto source_player = std::ranges::find_if(graph.instructions,
                  [source_clip](const CompiledInstruction& item) {
                    return item.type == NodeType::clip_player && item.clip_index == source_clip;
                  });
              const auto target_player = std::ranges::find_if(graph.instructions,
                  [target_clip](const CompiledInstruction& item) {
                    return item.type == NodeType::clip_player && item.clip_index == target_clip;
                  });
              if (source_player != graph.instructions.end() && target_player != graph.instructions.end()) {
                const auto source_instruction = static_cast<std::size_t>(source_player - graph.instructions.begin());
                const auto target_instruction = static_cast<std::size_t>(target_player - graph.instructions.begin());
                const auto synchronized = synchronize_to_marker(context.clips[source_clip],
                    context.clips[target_clip], *transition->sync_marker,
                    instance.clip_times[source_instruction]);
                if (!synchronized) return make_unexpected(synchronized.error());
                instance.clip_times[target_instruction] = *synchronized;
                result.sync_markers.push_back(*transition->sync_marker);
              }
            }
          }
        }
        const auto state_index = [&config](StateId id) -> std::size_t {
          const auto found = std::ranges::find(config->definition.states, id, &StateDefinition::id);
          return static_cast<std::size_t>(found - config->definition.states.begin());
        };
        const auto source_index = state_index(update->source);
        const auto target_index = state_index(update->target);
        if (source_index >= 2 || target_index >= 2)
          return make_unexpected(Error{ErrorCode::graph, "StateMachine state is not bound to a pose input"});
        const float weight = update->source == update->target ? 0.0F : update->alpha;
        const auto blended = blend_poses(instance.pose_slots[instruction.inputs[source_index].value],
                                         instance.pose_slots[instruction.inputs[target_index].value], weight);
        if (!blended) return make_unexpected(blended.error());
        copy_pose(output, *blended);
        if (weight < 1.0F)
          append_events(output_events, instance.event_slots[instruction.inputs[source_index].value]);
        if (weight > 0.0F)
          append_events(output_events, instance.event_slots[instruction.inputs[target_index].value]);
        const auto visible_state = config->definition.states[target_index].name;
        result.state = visible_state;
        result.transition_progress = update->source == update->target ? 1.0F : update->alpha;
        for (const auto& state_event : update->events) {
          output_events.push_back(RuntimeEventOccurrence{
              AnimationEvent{AnimTime{0}, state_event, 0}, AnimTime{0}, 0,
              instruction.node, std::numeric_limits<std::size_t>::max()});
        }
        result.blends.push_back(BlendObservation{instruction.node, instruction.name,
                                                 {1.0F - weight, weight}});
        root_output = blend_root_motion(instance.root_motion_slots[instruction.inputs[source_index].value],
                                        instance.root_motion_slots[instruction.inputs[target_index].value], weight);
        break;
      }
      case NodeType::output:
        copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
        append_events(output_events, instance.event_slots[instruction.inputs[0].value]);
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
  result.event_occurrences = instance.event_slots[graph.output_slot.value];
  for (const auto& occurrence : result.event_occurrences)
    result.events.push_back(occurrence.event);
  const auto accumulated = compose(instance.root_motion_accumulator, result.root_motion);
  if (!accumulated) return make_unexpected(accumulated.error());
  instance.root_motion_accumulator = *accumulated;
  result.root_accumulated = instance.root_motion_accumulator;
  instance.memo = result;
  instance.memo_frame = context.frame;
  instance.memo_generation = context.generation;
  instance.memo_parameter_hash = parameters;
  return result;
}

}  // namespace animgraph
