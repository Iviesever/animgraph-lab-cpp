#pragma once

#include "animgraph/clip/clip.hpp"
#include "animgraph/core/expected.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace animgraph {

struct StateId { std::uint32_t value{}; auto operator<=>(const StateId&) const = default; };
enum class CompareOp { less, less_equal, greater, greater_equal };
struct ParameterCondition { std::size_t parameter{}; CompareOp operation{}; float threshold{}; };
struct StateDefinition { StateId id; std::string name; AnimTime duration; };
struct TransitionDefinition {
  StateId source;
  StateId target;
  ParameterCondition condition;
  std::int32_t priority{};
  AnimTime blend_duration;
  std::optional<float> exit_time_normalized;
  std::optional<std::string> sync_marker;
  bool can_interrupt{};
};
struct StateMachineDefinition {
  std::vector<StateDefinition> states;
  std::vector<TransitionDefinition> transitions;
  StateId entry;
};
struct StateMachineInstance {
  StateId current;
  std::optional<StateId> target;
  AnimTime state_time;
  AnimTime transition_time;
  AnimTime transition_duration;
};
struct StateMachineUpdate {
  StateId source;
  StateId target;
  float alpha{};
  std::vector<std::string> events;
};

[[nodiscard]] Expected<void, Error> validate_state_machine(const StateMachineDefinition& definition);
[[nodiscard]] Expected<StateMachineInstance, Error> make_state_machine_instance(
    const StateMachineDefinition& definition);
[[nodiscard]] Expected<StateMachineUpdate, Error> update_state_machine(
    const StateMachineDefinition& definition, std::span<const float> parameters,
    AnimTime delta, StateMachineInstance& instance);
[[nodiscard]] Expected<AnimTime, Error> synchronize_to_marker(
    const AnimationClip& source, const AnimationClip& target, std::string_view marker,
    AnimTime source_time);

}  // namespace animgraph
