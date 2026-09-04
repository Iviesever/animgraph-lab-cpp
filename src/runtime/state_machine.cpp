#include "animgraph/runtime/state_machine.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <ranges>

namespace animgraph {
namespace {

bool matches(float value, const ParameterCondition& condition) {
  switch (condition.operation) {
    case CompareOp::less: return value < condition.threshold;
    case CompareOp::less_equal: return value <= condition.threshold;
    case CompareOp::greater: return value > condition.threshold;
    case CompareOp::greater_equal: return value >= condition.threshold;
  }
  return false;
}

const StateDefinition* find_state(const StateMachineDefinition& definition, StateId id) {
  const auto found = std::ranges::find(definition.states, id, &StateDefinition::id);
  return found == definition.states.end() ? nullptr : &*found;
}

}  // namespace

Expected<void, Error> validate_state_machine(const StateMachineDefinition& definition) {
  if (definition.states.empty())
    return make_unexpected(Error{ErrorCode::graph, "state machine has no states"});
  std::map<std::uint32_t, std::string_view> ids;
  for (const auto& state : definition.states) {
    if (state.name.empty() || state.duration.ticks < 0 || !ids.emplace(state.id.value, state.name).second)
      return make_unexpected(Error{ErrorCode::graph, "state definition is invalid"});
  }
  if (!ids.contains(definition.entry.value))
    return make_unexpected(Error{ErrorCode::graph, "entry state does not exist"});
  for (const auto& transition : definition.transitions) {
    if (!ids.contains(transition.source.value) || !ids.contains(transition.target.value) ||
        transition.source == transition.target || transition.blend_duration.ticks < 0 ||
        !std::isfinite(transition.condition.threshold) ||
        (transition.exit_time_normalized &&
         (!std::isfinite(*transition.exit_time_normalized) || *transition.exit_time_normalized < 0.0F ||
          *transition.exit_time_normalized > 1.0F)) ||
        (transition.sync_marker && transition.sync_marker->empty())) {
      return make_unexpected(Error{ErrorCode::graph, "transition definition is invalid"});
    }
  }
  return {};
}

Expected<StateMachineInstance, Error> make_state_machine_instance(
    const StateMachineDefinition& definition) {
  const auto valid = validate_state_machine(definition);
  if (!valid) return make_unexpected(valid.error());
  return StateMachineInstance{definition.entry, std::nullopt, AnimTime{0}, AnimTime{0}, AnimTime{0}};
}

Expected<StateMachineUpdate, Error> update_state_machine(
    const StateMachineDefinition& definition, std::span<const float> parameters,
    AnimTime delta, StateMachineInstance& instance) {
  const auto valid = validate_state_machine(definition);
  if (!valid) return make_unexpected(valid.error());
  if (delta.ticks < 0 || !find_state(definition, instance.current))
    return make_unexpected(Error{ErrorCode::invalid_argument, "state update input is invalid"});
  if (instance.target) {
    const auto active = std::ranges::find_if(definition.transitions, [&instance](const auto& transition) {
      return transition.source == instance.current && transition.target == *instance.target;
    });
    if (active != definition.transitions.end() && active->can_interrupt) {
      const TransitionDefinition* interrupted_by = nullptr;
      for (const auto& candidate : definition.transitions) {
        if (candidate.source != *instance.target || candidate.condition.parameter >= parameters.size() ||
            !std::isfinite(parameters[candidate.condition.parameter]) ||
            !matches(parameters[candidate.condition.parameter], candidate.condition)) continue;
        if (!interrupted_by || candidate.priority > interrupted_by->priority ||
            (candidate.priority == interrupted_by->priority &&
             candidate.target.value < interrupted_by->target.value)) {
          interrupted_by = &candidate;
        }
      }
      if (interrupted_by) {
        const StateId new_source = *instance.target;
        const auto* source_state = find_state(definition, new_source);
        const auto* target_state = find_state(definition, interrupted_by->target);
        StateMachineUpdate update{new_source, interrupted_by->target,
            interrupted_by->blend_duration.ticks == 0 ? 1.0F : 0.0F,
            static_cast<std::size_t>(interrupted_by - definition.transitions.data()),
            {"exit:" + source_state->name, "enter:" + target_state->name}};
        instance.current = new_source;
        instance.state_time = AnimTime{0};
        instance.transition_time = AnimTime{0};
        instance.transition_duration = interrupted_by->blend_duration;
        if (interrupted_by->blend_duration.ticks == 0) {
          instance.current = interrupted_by->target;
          instance.target.reset();
        } else {
          instance.target = interrupted_by->target;
        }
        return update;
      }
    }
    instance.transition_time.ticks += delta.ticks;
    const float alpha = instance.transition_duration.ticks == 0 ? 1.0F : std::clamp(
        static_cast<float>(instance.transition_time.ticks) /
        static_cast<float>(instance.transition_duration.ticks), 0.0F, 1.0F);
    const StateMachineUpdate update{instance.current, *instance.target, alpha,
        static_cast<std::size_t>(active - definition.transitions.begin()), {}};
    if (alpha >= 1.0F) {
      instance.current = *instance.target;
      instance.target.reset();
      instance.state_time = AnimTime{0};
      instance.transition_time = AnimTime{0};
    }
    return update;
  }

  const std::int64_t previous_state_time = instance.state_time.ticks;
  instance.state_time.ticks += delta.ticks;
  const auto* current = find_state(definition, instance.current);
  const TransitionDefinition* selected = nullptr;
  for (const auto& transition : definition.transitions) {
    if (transition.source != instance.current || transition.condition.parameter >= parameters.size() ||
        !std::isfinite(parameters[transition.condition.parameter]) ||
        !matches(parameters[transition.condition.parameter], transition.condition)) continue;
    if (transition.exit_time_normalized) {
      if (current->duration.ticks > 0) {
        const auto duration = current->duration.ticks;
        const auto threshold = static_cast<std::int64_t>(
            std::llround(*transition.exit_time_normalized * static_cast<float>(duration)));
        std::int64_t crossing = (previous_state_time / duration) * duration + threshold;
        const bool initial_zero_crossing = previous_state_time == 0 && threshold == 0;
        if (crossing < previous_state_time ||
            (crossing == previous_state_time && !initial_zero_crossing)) {
          crossing += duration;
        }
        if (crossing > instance.state_time.ticks) continue;
      }
    }
    if (!selected || transition.priority > selected->priority ||
        (transition.priority == selected->priority && transition.target.value < selected->target.value)) {
      selected = &transition;
    }
  }
  if (!selected) return StateMachineUpdate{instance.current, instance.current, 0.0F,
                                           std::nullopt, {}};
  const auto* target = find_state(definition, selected->target);
  StateMachineUpdate update{instance.current, selected->target,
                            selected->blend_duration.ticks == 0 ? 1.0F : 0.0F,
                            static_cast<std::size_t>(selected - definition.transitions.data()),
                            {"exit:" + current->name, "enter:" + target->name}};
  if (selected->blend_duration.ticks == 0) {
    instance.current = selected->target;
    instance.state_time = AnimTime{0};
  } else {
    instance.target = selected->target;
    instance.transition_time = AnimTime{0};
    instance.transition_duration = selected->blend_duration;
  }
  return update;
}

Expected<AnimTime, Error> synchronize_to_marker(
    const AnimationClip& source, const AnimationClip& target, std::string_view marker,
    AnimTime source_time) {
  if (marker.empty() || source.duration.ticks <= 0 || target.duration.ticks <= 0)
    return make_unexpected(Error{ErrorCode::invalid_argument, "marker synchronization input is invalid"});
  const auto source_marker = std::ranges::find(source.markers, marker, &SyncMarker::name);
  const auto target_marker = std::ranges::find(target.markers, marker, &SyncMarker::name);
  if (source_marker == source.markers.end() || target_marker == target.markers.end())
    return make_unexpected(Error{ErrorCode::invalid_argument, "sync marker is missing"});
  const auto source_local = normalize_time(source_time, source.duration, ClipPlaybackMode::loop).value().local;
  const std::int64_t offset = source_local.ticks - source_marker->time.ticks;
  const double normalized_offset = static_cast<double>(offset) / static_cast<double>(source.duration.ticks);
  std::int64_t target_tick = target_marker->time.ticks +
      static_cast<std::int64_t>(std::llround(normalized_offset * static_cast<double>(target.duration.ticks)));
  target_tick %= target.duration.ticks;
  if (target_tick < 0) target_tick += target.duration.ticks;
  return AnimTime{target_tick};
}

}  // namespace animgraph
