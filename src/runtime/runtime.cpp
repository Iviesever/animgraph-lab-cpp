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

bool same_occurrence(const RuntimeEventOccurrence& left,
                     const RuntimeEventOccurrence& right) {
  return left.absolute_time == right.absolute_time && left.cycle == right.cycle &&
         left.source_node == right.source_node && left.clip_index == right.clip_index &&
         left.event.time == right.event.time && left.event.name == right.event.name &&
         left.event.payload == right.event.payload;
}

bool same_occurrence(const RuntimeSyncMarkerOccurrence& left,
                     const RuntimeSyncMarkerOccurrence& right) {
  return left.absolute_time == right.absolute_time && left.cycle == right.cycle &&
         left.source_node == right.source_node && left.clip_index == right.clip_index &&
         left.marker.time == right.marker.time && left.marker.name == right.marker.name;
}

template <class Occurrence, class Range>
void append_unique(std::vector<Occurrence>& destination,
                   const Range& source) {
  for (const auto& occurrence : source) {
    if (std::ranges::none_of(destination, [&occurrence](const Occurrence& existing) {
          return same_occurrence(existing, occurrence);
        })) {
      destination.push_back(occurrence);
    }
  }
}

std::size_t required_inputs(NodeType type) {
  switch (type) {
    case NodeType::reference_pose:
    case NodeType::clip_player: return 0;
    case NodeType::pose_cache:
    case NodeType::two_bone_ik:
    case NodeType::output: return 1;
    case NodeType::blend_1d:
    case NodeType::additive:
    case NodeType::layered_blend_per_bone:
    case NodeType::state_machine: return 2;
    case NodeType::blend_2d: return 3;
  }
  return max_graph_nodes;
}

std::size_t required_parameters(NodeType type) {
  switch (type) {
    case NodeType::blend_1d:
    case NodeType::additive:
    case NodeType::layered_blend_per_bone: return 1;
    case NodeType::blend_2d: return 2;
    case NodeType::two_bone_ik: return 4;
    default: return 0;
  }
}

std::uint32_t required_state_size(NodeType type) {
  switch (type) {
    case NodeType::clip_player: return 8;
    case NodeType::pose_cache: return 24;
    case NodeType::state_machine: return 32;
    default: return 0;
  }
}

bool config_matches(NodeType type, const NodeConfig& config) {
  switch (type) {
    case NodeType::clip_player: return std::holds_alternative<ClipPlayerNodeConfig>(config);
    case NodeType::blend_1d: return std::holds_alternative<Blend1DNodeConfig>(config);
    case NodeType::blend_2d: return std::holds_alternative<Blend2DNodeConfig>(config);
    case NodeType::additive: return std::holds_alternative<AdditiveNodeConfig>(config);
    case NodeType::layered_blend_per_bone: return std::holds_alternative<LayeredNodeConfig>(config);
    case NodeType::two_bone_ik: return std::holds_alternative<TwoBoneIkNodeConfig>(config);
    case NodeType::state_machine: return std::holds_alternative<StateMachineNodeConfig>(config);
    case NodeType::reference_pose:
    case NodeType::pose_cache:
    case NodeType::output: return std::holds_alternative<std::monostate>(config);
  }
  return false;
}

}  // namespace

Expected<void, Error> validate_instance_layout(const CompiledGraph& graph,
                                               const GraphInstance& instance,
                                               std::size_t joint_count) {
  if (instance.pose_slots.size() != graph.pose_slot_count ||
      instance.initialized.size() != graph.pose_slot_count ||
      instance.root_motion_slots.size() != graph.pose_slot_count ||
      instance.event_slots.size() != graph.pose_slot_count ||
      instance.marker_slots.size() != graph.pose_slot_count ||
      instance.clip_times.size() != graph.instructions.size() ||
      instance.pose_caches.size() != graph.instructions.size() ||
      instance.state_nodes.size() != graph.instructions.size() ||
      instance.state_machines.size() != graph.instructions.size() ||
      instance.state_updates.size() != graph.instructions.size() ||
      instance.clip_time_overrides.size() != graph.instructions.size() ||
      instance.clip_time_discontinuities.size() != graph.instructions.size() ||
      instance.state_sync_markers.size() != graph.instructions.size() ||
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
  for (std::size_t index = 0; index < graph.instructions.size(); ++index) {
    const auto& instruction = graph.instructions[index];
    if (instruction.output.value >= graph.pose_slot_count ||
        instruction.inputs.size() != required_inputs(instruction.type) ||
        instruction.parameter_indices.size() != required_parameters(instruction.type) ||
        !config_matches(instruction.type, instruction.config) ||
        instruction.state_size != required_state_size(instruction.type) ||
        instruction.state_offset % 8U != 0U ||
        instruction.state_offset > graph.state_size ||
        instruction.state_size > graph.state_size - instruction.state_offset)
      return make_unexpected(Error{ErrorCode::graph, "compiled output slot is invalid"});
    for (const auto parameter : instruction.parameter_indices) {
      if (parameter != std::numeric_limits<std::uint32_t>::max() &&
          parameter >= graph.parameters.size())
        return make_unexpected(Error{ErrorCode::graph, "compiled parameter index is invalid"});
    }
    for (const auto input : instruction.inputs) {
      if (input.value >= graph.pose_slot_count)
        return make_unexpected(Error{ErrorCode::graph, "compiled input slot is invalid"});
    }
    if (instruction.type == NodeType::clip_player && !instruction.clip_index)
      return make_unexpected(Error{ErrorCode::graph, "compiled clip binding is missing"});
    if (const auto* state = std::get_if<StateMachineNodeConfig>(&instruction.config)) {
      for (const auto player : state->sync_player_indices)
        if (player && (*player >= graph.instructions.size() ||
                       graph.instructions[*player].type != NodeType::clip_player))
          return make_unexpected(Error{ErrorCode::graph, "compiled sync player is invalid"});
    }
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
  instance.marker_slots.resize(graph.pose_slot_count);
  instance.initialized.resize(graph.pose_slot_count);
  instance.pose_caches.resize(graph.instructions.size());
  instance.state_nodes.resize(graph.instructions.size());
  instance.state_machines.resize(graph.instructions.size());
  instance.state_updates.resize(graph.instructions.size());
  instance.clip_time_overrides.resize(graph.instructions.size());
  instance.clip_time_discontinuities.resize(graph.instructions.size());
  instance.state_sync_markers.resize(graph.instructions.size());
  instance.state.resize(graph.state_size);
  for (auto& pose : instance.pose_slots) pose.transforms.resize(joint_count);
  for (auto& cache : instance.pose_caches) cache.pose.transforms.resize(joint_count);
  for (auto& events : instance.event_slots) events.reserve(max_events);
  for (auto& markers : instance.marker_slots) markers.reserve(max_events);
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
  for (auto& markers : instance.marker_slots) markers.clear();
  for (auto& update : instance.state_updates) update.reset();
  for (auto& override_time : instance.clip_time_overrides) override_time.reset();
  std::ranges::fill(instance.clip_time_discontinuities, std::uint8_t{0});
  for (auto& marker : instance.state_sync_markers) marker.reset();
  EvaluationResult result;
  result.events.reserve(max_events);
  result.event_occurrences.reserve(max_events);
  result.sync_markers.reserve(max_events);
  result.sync_marker_occurrences.reserve(max_events);
  const auto parameter = [&context](const CompiledInstruction& instruction,
                                    std::size_t binding, float fallback) {
    if (binding >= instruction.parameter_indices.size()) return fallback;
    const auto index = instruction.parameter_indices[binding];
    return index < context.parameters.size() ? context.parameters[index] : fallback;
  };
  const auto advanced_time = [&context](AnimTime previous) -> Expected<AnimTime, Error> {
    if ((context.delta.ticks > 0 &&
         previous.ticks > std::numeric_limits<std::int64_t>::max() - context.delta.ticks) ||
        (context.delta.ticks < 0 &&
         previous.ticks < std::numeric_limits<std::int64_t>::min() - context.delta.ticks)) {
      return make_unexpected(Error{ErrorCode::bounds, "clip time overflow"});
    }
    return AnimTime{previous.ticks + context.delta.ticks};
  };

  // State changes depend only on instance state, parameters, and delta. Resolve them before
  // sampling so marker synchronization can adjust the exact target player for this frame.
  for (std::size_t index = 0; index < graph.instructions.size(); ++index) {
    const auto& instruction = graph.instructions[index];
    if (instruction.type != NodeType::state_machine) continue;
    const auto* config = std::get_if<StateMachineNodeConfig>(&instruction.config);
    if (!config || !instance.state_machines[index])
      return make_unexpected(Error{ErrorCode::graph, "StateMachine config or instance is missing"});
    auto update = update_state_machine(config->definition, context.parameters,
                                       context.delta, *instance.state_machines[index]);
    if (!update) return make_unexpected(update.error());
    instance.state_updates[index] = *update;
    if (update->events.empty()) continue;
    if (!update->transition_index ||
        *update->transition_index >= config->definition.transitions.size()) {
      return make_unexpected(Error{ErrorCode::graph, "selected state transition is invalid"});
    }
    const auto& transition = config->definition.transitions[*update->transition_index];
    if (!transition.sync_marker) continue;
    const auto state_position = [&config](StateId id) -> std::size_t {
      const auto found = std::ranges::find(config->definition.states, id, &StateDefinition::id);
      return static_cast<std::size_t>(found - config->definition.states.begin());
    };
    const auto source_state = state_position(update->source);
    const auto target_state = state_position(update->target);
    if (source_state >= config->sync_player_indices.size() ||
        target_state >= config->sync_player_indices.size() ||
        !config->sync_player_indices[source_state] ||
        !config->sync_player_indices[target_state] ||
        !config->sync_clip_indices[target_state]) {
      return make_unexpected(Error{ErrorCode::graph, "state sync player binding is incomplete"});
    }
    const auto source_player = *config->sync_player_indices[source_state];
    const auto target_player = *config->sync_player_indices[target_state];
    const auto source_clip = graph.instructions[source_player].clip_index;
    const auto target_clip = graph.instructions[target_player].clip_index;
    if (!source_clip || !target_clip || *source_clip >= context.clips.size() ||
        *target_clip >= context.clips.size()) {
      return make_unexpected(Error{ErrorCode::bounds, "state sync clip binding is invalid"});
    }
    auto source_time = instance.clip_time_overrides[source_player]
        ? Expected<AnimTime, Error>{*instance.clip_time_overrides[source_player]}
        : advanced_time(instance.clip_times[source_player]);
    if (!source_time) return make_unexpected(source_time.error());
    const auto synchronized_local = synchronize_to_marker(context.clips[*source_clip],
        context.clips[*target_clip], *transition.sync_marker, *source_time);
    if (!synchronized_local) return make_unexpected(synchronized_local.error());
    const auto duration = context.clips[*target_clip].duration.ticks;
    const auto previous_target = instance.clip_times[target_player].ticks;
    const long double cycle = std::floor(static_cast<long double>(previous_target) /
                                         static_cast<long double>(duration));
    long double absolute_wide = cycle * static_cast<long double>(duration) +
                                static_cast<long double>(synchronized_local->ticks);
    if (absolute_wide < static_cast<long double>(previous_target)) absolute_wide += duration;
    if (absolute_wide < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        absolute_wide > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
      return make_unexpected(Error{ErrorCode::bounds, "synchronized clip time overflows"});
    }
    const AnimTime synchronized{static_cast<std::int64_t>(absolute_wide)};
    if (instance.clip_time_overrides[target_player] &&
        *instance.clip_time_overrides[target_player] != synchronized) {
      return make_unexpected(Error{ErrorCode::graph, "conflicting state sync overrides"});
    }
    instance.clip_time_overrides[target_player] = synchronized;
    instance.clip_time_discontinuities[target_player] = 1;
    const auto marker = std::ranges::find(context.clips[*target_clip].markers,
                                          *transition.sync_marker, &SyncMarker::name);
    if (marker == context.clips[*target_clip].markers.end())
      return make_unexpected(Error{ErrorCode::invalid_argument, "target sync marker is missing"});
    instance.state_sync_markers[index] = RuntimeSyncMarkerOccurrence{
        *marker, synchronized, synchronized.ticks / duration,
        instruction.node, *target_clip};
  }

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
    auto& output_markers = instance.marker_slots[instruction.output.value];
    output_events.clear();
    output_markers.clear();
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
        const auto current = instance.clip_time_overrides[index]
            ? Expected<AnimTime, Error>{*instance.clip_time_overrides[index]}
            : advanced_time(previous);
        if (!current) return make_unexpected(current.error());
        instance.clip_times[index] = *current;
        const AnimTime interval_start = instance.clip_time_discontinuities[index]
            ? *current : previous;
        const auto sampled = sample_clip(context.skeleton, context.clips[*instruction.clip_index],
                                         instance.clip_times[index]);
        if (!sampled) return make_unexpected(sampled.error());
        copy_pose(output, sampled->pose);
        if (!context.skeleton.joints.empty()) {
          const auto motion = extract_root_motion(context.skeleton,
              context.clips[*instruction.clip_index], config->root_motion_joint, interval_start,
              instance.clip_times[index]);
          if (!motion) return make_unexpected(motion.error());
          root_output = *motion;
        }
        if (config->remove_root_motion)
          apply_root_motion_policy(output, config->root_motion_joint,
                                   RootMotionPosePolicy::remove);
        const auto occurrences = query_event_occurrences(
            context.clips[*instruction.clip_index], interval_start, instance.clip_times[index]);
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
            marker_clip, interval_start, instance.clip_times[index]);
        if (marker_occurrences) {
          for (const auto& occurrence : *marker_occurrences)
            output_markers.push_back(RuntimeSyncMarkerOccurrence{
                SyncMarker{occurrence.event.time, occurrence.event.name},
                occurrence.absolute_time, occurrence.cycle, instruction.node,
                *instruction.clip_index});
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
        if (weight < 1.0F) {
          append_unique(output_events, instance.event_slots[instruction.inputs[0].value]);
          append_unique(output_markers, instance.marker_slots[instruction.inputs[0].value]);
        }
        if (weight > 0.0F) {
          append_unique(output_events, instance.event_slots[instruction.inputs[1].value]);
          append_unique(output_markers, instance.marker_slots[instruction.inputs[1].value]);
        }
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
          if (blended->weights[input] > 0.0F) {
            append_unique(output_events, instance.event_slots[instruction.inputs[input].value]);
            append_unique(output_markers, instance.marker_slots[instruction.inputs[input].value]);
          }
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
        append_unique(output_events, instance.event_slots[instruction.inputs[0].value]);
        append_unique(output_markers, instance.marker_slots[instruction.inputs[0].value]);
        if (weight > 0.0F) {
          append_unique(output_events, instance.event_slots[instruction.inputs[1].value]);
          append_unique(output_markers, instance.marker_slots[instruction.inputs[1].value]);
        }
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
        append_unique(output_events, instance.event_slots[instruction.inputs[0].value]);
        append_unique(output_markers, instance.marker_slots[instruction.inputs[0].value]);
        const bool layer_contributes = std::ranges::any_of(weights, [](const float value) {
          return value > 0.0F;
        });
        if (layer_contributes) {
          append_unique(output_events, instance.event_slots[instruction.inputs[1].value]);
          append_unique(output_markers, instance.marker_slots[instruction.inputs[1].value]);
        }
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
        append_unique(output_events, instance.event_slots[instruction.inputs[0].value]);
        append_unique(output_markers, instance.marker_slots[instruction.inputs[0].value]);
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
          output_markers = cache.markers;
          root_output = cache.root_motion;
          ++result.pose_cache_hits;
        } else {
          copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
          copy_pose(cache.pose, output);
          append_unique(output_events, instance.event_slots[instruction.inputs[0].value]);
          append_unique(output_markers, instance.marker_slots[instruction.inputs[0].value]);
          cache.events = output_events;
          cache.markers = output_markers;
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
        if (!config || !instance.state_updates[index])
          return make_unexpected(Error{ErrorCode::graph, "StateMachine update is missing"});
        const auto& update = *instance.state_updates[index];
        const auto state_index = [&config](StateId id) -> std::size_t {
          const auto found = std::ranges::find(config->definition.states, id, &StateDefinition::id);
          return static_cast<std::size_t>(found - config->definition.states.begin());
        };
        const auto source_index = state_index(update.source);
        const auto target_index = state_index(update.target);
        if (source_index >= 2 || target_index >= 2)
          return make_unexpected(Error{ErrorCode::graph, "StateMachine state is not bound to a pose input"});
        const float weight = update.source == update.target ? 0.0F : update.alpha;
        const auto blended = blend_poses(instance.pose_slots[instruction.inputs[source_index].value],
                                         instance.pose_slots[instruction.inputs[target_index].value], weight);
        if (!blended) return make_unexpected(blended.error());
        copy_pose(output, *blended);
        if (weight < 1.0F) {
          append_unique(output_events, instance.event_slots[instruction.inputs[source_index].value]);
          append_unique(output_markers, instance.marker_slots[instruction.inputs[source_index].value]);
        }
        if (weight > 0.0F) {
          append_unique(output_events, instance.event_slots[instruction.inputs[target_index].value]);
          append_unique(output_markers, instance.marker_slots[instruction.inputs[target_index].value]);
        }
        if (instance.state_sync_markers[index]) {
          const std::array marker{*instance.state_sync_markers[index]};
          append_unique(output_markers, marker);
        }
        const auto visible_state = config->definition.states[target_index].name;
        result.state = visible_state;
        result.transition_progress = update.source == update.target ? 1.0F : update.alpha;
        for (const auto& state_event : update.events) {
          const std::array occurrence{RuntimeEventOccurrence{
              AnimationEvent{AnimTime{0}, state_event, 0}, AnimTime{0}, 0,
              instruction.node, std::numeric_limits<std::size_t>::max()}};
          append_unique(output_events, occurrence);
        }
        result.blends.push_back(BlendObservation{instruction.node, instruction.name,
                                                 {1.0F - weight, weight}});
        root_output = blend_root_motion(instance.root_motion_slots[instruction.inputs[source_index].value],
                                        instance.root_motion_slots[instruction.inputs[target_index].value], weight);
        break;
      }
      case NodeType::output:
        copy_pose(output, instance.pose_slots[instruction.inputs[0].value]);
        append_unique(output_events, instance.event_slots[instruction.inputs[0].value]);
        append_unique(output_markers, instance.marker_slots[instruction.inputs[0].value]);
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
  result.sync_marker_occurrences = instance.marker_slots[graph.output_slot.value];
  for (const auto& occurrence : result.sync_marker_occurrences)
    result.sync_markers.push_back(occurrence.marker.name);
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
