#include "animgraph/graph/graph.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <limits>
#include <ranges>
#include <set>
#include <sstream>
#include <unordered_map>

namespace animgraph {
namespace {

std::size_t required_pose_inputs(NodeType type) {
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

std::uint32_t node_state_size(NodeType type) {
  switch (type) {
    case NodeType::clip_player: return 8;
    case NodeType::pose_cache: return 24;
    case NodeType::state_machine: return 32;
    default: return 0;
  }
}

std::string escape_json(std::string_view value) {
  std::string result;
  for (const char character : value) {
    switch (character) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (static_cast<unsigned char>(character) < 0x20U) result += '?';
        else result += character;
    }
  }
  return result;
}

std::uint32_t align_eight(std::uint32_t value) { return (value + 7U) & ~7U; }

std::uint64_t fnv1a(std::string_view value) noexcept {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

StateMachineNodeConfig default_state_machine_config() {
  StateMachineNodeConfig config;
  config.definition.states = {{StateId{0}, "Idle", AnimTime{48'000}},
                              {StateId{1}, "Locomotion", AnimTime{48'000}}};
  config.definition.entry = StateId{0};
  config.definition.transitions = {
      TransitionDefinition{StateId{0}, StateId{1},
          ParameterCondition{0, CompareOp::greater_equal, 0.5F}, 1,
          AnimTime{12'000}, std::nullopt, std::nullopt, true},
      TransitionDefinition{StateId{1}, StateId{0},
          ParameterCondition{0, CompareOp::less, 0.5F}, 1,
          AnimTime{12'000}, std::nullopt, std::nullopt, true}};
  return config;
}

NodeConfig default_config(NodeType type) {
  switch (type) {
    case NodeType::clip_player: return ClipPlayerNodeConfig{};
    case NodeType::blend_1d: return Blend1DNodeConfig{};
    case NodeType::blend_2d: return Blend2DNodeConfig{};
    case NodeType::additive: return AdditiveNodeConfig{};
    case NodeType::layered_blend_per_bone: return LayeredNodeConfig{};
    case NodeType::two_bone_ik: return TwoBoneIkNodeConfig{};
    case NodeType::state_machine: return default_state_machine_config();
    default: return std::monostate{};
  }
}

template <class Config>
Expected<void, Error> set_config(GraphDescription& description, NodeId node,
                                 NodeType expected_type, Config config) {
  const auto found = std::ranges::find(description.nodes, node, &NodeDefinition::id);
  if (found == description.nodes.end() || found->type != expected_type)
    return make_unexpected(Error{ErrorCode::graph, "node configuration targets the wrong node type"});
  found->config = std::move(config);
  return {};
}

Expected<void, Error> bind_parameter(NodeConfig& config, std::uint16_t slot,
                                     std::string parameter) {
  if (parameter.empty())
    return make_unexpected(Error{ErrorCode::graph, "parameter binding name is empty"});
  if (auto* blend = std::get_if<Blend1DNodeConfig>(&config); blend && slot == 0) {
    blend->parameter = std::move(parameter); return {};
  }
  if (auto* blend = std::get_if<Blend2DNodeConfig>(&config); blend && slot < 2) {
    blend->parameters[slot] = std::move(parameter); return {};
  }
  if (auto* additive = std::get_if<AdditiveNodeConfig>(&config); additive && slot == 0) {
    additive->parameter = std::move(parameter); return {};
  }
  if (auto* layer = std::get_if<LayeredNodeConfig>(&config); layer && slot == 0) {
    layer->parameter = std::move(parameter); return {};
  }
  if (auto* ik = std::get_if<TwoBoneIkNodeConfig>(&config); ik && slot < 4) {
    ik->parameters[slot] = std::move(parameter); return {};
  }
  return make_unexpected(Error{ErrorCode::graph, "value pin does not exist on target node"});
}

void write_float_bits(std::ostream& output, float value) {
  output << std::bit_cast<std::uint32_t>(value);
}

void write_config(std::ostream& output, const NodeConfig& config) {
  output << ",\"config\":{";
  if (const auto* clip = std::get_if<ClipPlayerNodeConfig>(&config)) {
    output << "\"root\":" << clip->root_motion_joint.value << ",\"remove_root\":"
           << (clip->remove_root_motion ? "true" : "false");
  } else if (const auto* blend1d = std::get_if<Blend1DNodeConfig>(&config)) {
    output << "\"threshold_bits\":["; write_float_bits(output, blend1d->thresholds[0]);
    output << ','; write_float_bits(output, blend1d->thresholds[1]); output << "],\"fallback_bits\":";
    write_float_bits(output, blend1d->fallback);
  } else if (const auto* blend2d = std::get_if<Blend2DNodeConfig>(&config)) {
    output << "\"point_bits\":[";
    for (std::size_t index = 0; index < blend2d->points.size(); ++index) {
      if (index) output << ',';
      output << '['; write_float_bits(output, blend2d->points[index].x); output << ',';
      write_float_bits(output, blend2d->points[index].y); output << ']';
    }
    output << "],\"fallback_bits\":["; write_float_bits(output, blend2d->fallbacks[0]);
    output << ','; write_float_bits(output, blend2d->fallbacks[1]); output << ']';
  } else if (const auto* additive = std::get_if<AdditiveNodeConfig>(&config)) {
    output << "\"fallback_bits\":"; write_float_bits(output, additive->fallback);
  } else if (const auto* layer = std::get_if<LayeredNodeConfig>(&config)) {
    output << "\"fallback_bits\":"; write_float_bits(output, layer->fallback);
    output << ",\"mask_bits\":[";
    for (std::size_t index = 0; index < layer->joint_weights.size(); ++index) {
      if (index) output << ','; write_float_bits(output, layer->joint_weights[index]);
    }
    output << ']';
  } else if (const auto* ik = std::get_if<TwoBoneIkNodeConfig>(&config)) {
    output << "\"chain\":[" << ik->root.value << ',' << ik->mid.value << ',' << ik->end.value
           << "],\"pole_bits\":["; write_float_bits(output, ik->pole.x); output << ',';
    write_float_bits(output, ik->pole.y); output << ','; write_float_bits(output, ik->pole.z);
    output << "],\"fallback_bits\":[";
    for (std::size_t index = 0; index < ik->fallbacks.size(); ++index) {
      if (index) output << ','; write_float_bits(output, ik->fallbacks[index]);
    }
    output << "],\"limit\":";
    if (ik->limit) {
      output << '['; write_float_bits(output, ik->limit->min_bend_radians); output << ',';
      write_float_bits(output, ik->limit->max_bend_radians); output << ']';
    } else output << "null";
  } else if (const auto* state = std::get_if<StateMachineNodeConfig>(&config)) {
    output << "\"entry\":" << state->definition.entry.value << ",\"states\":[";
    for (std::size_t index = 0; index < state->definition.states.size(); ++index) {
      if (index) output << ',';
      const auto& item = state->definition.states[index];
      output << "{\"id\":" << item.id.value << ",\"name\":\"" << escape_json(item.name)
             << "\",\"duration\":" << item.duration.ticks << '}';
    }
    output << "],\"transitions\":[";
    for (std::size_t index = 0; index < state->definition.transitions.size(); ++index) {
      if (index) output << ',';
      const auto& item = state->definition.transitions[index];
      output << "{\"source\":" << item.source.value << ",\"target\":" << item.target.value
             << ",\"parameter\":" << item.condition.parameter << ",\"operation\":"
             << static_cast<int>(item.condition.operation) << ",\"threshold_bits\":";
      write_float_bits(output, item.condition.threshold);
      output << ",\"priority\":" << item.priority << ",\"blend\":"
             << item.blend_duration.ticks << ",\"interrupt\":"
             << (item.can_interrupt ? "true" : "false") << ",\"exit_bits\":";
      if (item.exit_time_normalized) write_float_bits(output, *item.exit_time_normalized);
      else output << "null";
      output << ",\"marker\":";
      if (item.sync_marker) output << '"' << escape_json(*item.sync_marker) << '"';
      else output << "null";
      output << '}';
    }
    output << "],\"sync_clips\":[";
    for (std::size_t index = 0; index < state->sync_clip_indices.size(); ++index) {
      if (index) output << ',';
      if (state->sync_clip_indices[index]) output << *state->sync_clip_indices[index];
      else output << "null";
    }
    output << ']';
  } else {
    output << "\"none\":true";
  }
  output << '}';
}

}  // namespace

NodeId GraphBuilder::add_node(NodeType type, std::string name) {
  const NodeId id{static_cast<std::uint32_t>(description_.nodes.size())};
  description_.nodes.push_back(NodeDefinition{.id = id, .type = type, .name = std::move(name),
                                               .clip_index = std::nullopt,
                                               .config = default_config(type)});
  return id;
}
Expected<void, Error> GraphBuilder::connect(PosePin source, PosePin target) {
  description_.connections.push_back(Connection{source.node, source.index, target.node,
                                                 target.index, PinType::pose});
  return {};
}
Expected<void, Error> GraphBuilder::connect(ValuePin source, ValuePin target) {
  if (!source.node && !source.parameter.empty() && target.node) {
    const auto found = std::ranges::find(description_.nodes, *target.node, &NodeDefinition::id);
    if (found == description_.nodes.end())
      return make_unexpected(Error{ErrorCode::graph, "value target node does not exist"});
    return bind_parameter(found->config, target.index, std::move(source.parameter));
  }
  return make_unexpected(Error{ErrorCode::graph, "value connection requires parameter source and node target"});
}
Expected<void, Error> GraphBuilder::set_clip(NodeId node, std::size_t clip_index) {
  const auto found = std::ranges::find(description_.nodes, node, &NodeDefinition::id);
  if (found == description_.nodes.end() || found->type != NodeType::clip_player) {
    return make_unexpected(Error{ErrorCode::graph, "clip binding targets a non-player node"});
  }
  found->clip_index = clip_index;
  return {};
}
Expected<void, Error> GraphBuilder::configure_clip_player(
    NodeId node, JointId root_motion_joint, bool remove_root_motion) {
  return set_config(description_, node, NodeType::clip_player,
                    ClipPlayerNodeConfig{root_motion_joint, remove_root_motion});
}
Expected<void, Error> GraphBuilder::configure_blend_1d(
    NodeId node, std::array<float, 2> thresholds, std::string parameter, float fallback) {
  return set_config(description_, node, NodeType::blend_1d,
                    Blend1DNodeConfig{thresholds, std::move(parameter), fallback});
}
Expected<void, Error> GraphBuilder::configure_blend_2d(
    NodeId node, std::array<GraphPoint2, 3> points, std::array<std::string, 2> parameters,
    std::array<float, 2> fallbacks) {
  return set_config(description_, node, NodeType::blend_2d,
                    Blend2DNodeConfig{points, std::move(parameters), fallbacks});
}
Expected<void, Error> GraphBuilder::configure_additive(
    NodeId node, std::string parameter, float fallback) {
  return set_config(description_, node, NodeType::additive,
                    AdditiveNodeConfig{std::move(parameter), fallback});
}
Expected<void, Error> GraphBuilder::configure_layered(
    NodeId node, std::vector<float> joint_weights, std::string parameter, float fallback) {
  return set_config(description_, node, NodeType::layered_blend_per_bone,
                    LayeredNodeConfig{std::move(joint_weights), std::move(parameter), fallback});
}
Expected<void, Error> GraphBuilder::configure_two_bone_ik(
    NodeId node, TwoBoneIkNodeConfig config) {
  return set_config(description_, node, NodeType::two_bone_ik, std::move(config));
}
Expected<void, Error> GraphBuilder::configure_state_machine(
    NodeId node, StateMachineNodeConfig config) {
  return set_config(description_, node, NodeType::state_machine, std::move(config));
}
void GraphBuilder::set_output(NodeId output) noexcept { description_.output = output; }
void GraphBuilder::add_parameter(GraphParameter parameter) {
  description_.parameters.push_back(std::move(parameter));
}
GraphDescription GraphBuilder::build() const { return description_; }

std::string_view node_type_name(NodeType type) noexcept {
  switch (type) {
    case NodeType::reference_pose: return "ReferencePose";
    case NodeType::clip_player: return "ClipPlayer";
    case NodeType::blend_1d: return "Blend1D";
    case NodeType::blend_2d: return "Blend2D";
    case NodeType::additive: return "Additive";
    case NodeType::layered_blend_per_bone: return "LayeredBlendPerBone";
    case NodeType::pose_cache: return "PoseCache";
    case NodeType::two_bone_ik: return "TwoBoneIK";
    case NodeType::state_machine: return "StateMachine";
    case NodeType::output: return "Output";
  }
  return "Unknown";
}

Expected<CompiledGraph, Error> compile_graph(const GraphDescription& description,
                                             bool reuse_slots) {
  if (description.nodes.empty() || description.nodes.size() > max_graph_nodes || !description.output) {
    return make_unexpected(Error{ErrorCode::graph, "graph node count or output is invalid"});
  }
  std::map<std::uint32_t, const NodeDefinition*> nodes;
  for (const auto& node : description.nodes) {
    if (node.name.empty() || !nodes.emplace(node.id.value, &node).second) {
      return make_unexpected(Error{ErrorCode::graph, "node id or name is invalid"});
    }
    if (node.type == NodeType::clip_player && !node.clip_index) {
      return make_unexpected(Error{ErrorCode::graph, "clip player is not bound"});
    }
    const bool config_matches =
        (node.type == NodeType::clip_player && std::holds_alternative<ClipPlayerNodeConfig>(node.config)) ||
        (node.type == NodeType::blend_1d && std::holds_alternative<Blend1DNodeConfig>(node.config)) ||
        (node.type == NodeType::blend_2d && std::holds_alternative<Blend2DNodeConfig>(node.config)) ||
        (node.type == NodeType::additive && std::holds_alternative<AdditiveNodeConfig>(node.config)) ||
        (node.type == NodeType::layered_blend_per_bone && std::holds_alternative<LayeredNodeConfig>(node.config)) ||
        (node.type == NodeType::two_bone_ik && std::holds_alternative<TwoBoneIkNodeConfig>(node.config)) ||
        (node.type == NodeType::state_machine && std::holds_alternative<StateMachineNodeConfig>(node.config)) ||
        ((node.type == NodeType::reference_pose || node.type == NodeType::pose_cache ||
          node.type == NodeType::output) && std::holds_alternative<std::monostate>(node.config));
    if (!config_matches)
      return make_unexpected(Error{ErrorCode::graph, "node configuration type mismatch"});
    if (const auto* clip = std::get_if<ClipPlayerNodeConfig>(&node.config);
        clip && clip->root_motion_joint.value >= max_joints)
      return make_unexpected(Error{ErrorCode::graph, "clip root motion joint is invalid"});
    if (const auto* blend = std::get_if<Blend1DNodeConfig>(&node.config);
        blend && (!std::isfinite(blend->thresholds[0]) || !std::isfinite(blend->thresholds[1]) ||
                  blend->thresholds[0] >= blend->thresholds[1] || !std::isfinite(blend->fallback)))
      return make_unexpected(Error{ErrorCode::graph, "Blend1D configuration is invalid"});
    if (const auto* blend = std::get_if<Blend2DNodeConfig>(&node.config); blend) {
      for (const auto& point : blend->points)
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
          return make_unexpected(Error{ErrorCode::graph, "Blend2D point is invalid"});
      if (!std::isfinite(blend->fallbacks[0]) || !std::isfinite(blend->fallbacks[1]))
        return make_unexpected(Error{ErrorCode::graph, "Blend2D fallback is invalid"});
    }
    if (const auto* additive = std::get_if<AdditiveNodeConfig>(&node.config);
        additive && (!std::isfinite(additive->fallback) || additive->fallback < 0 || additive->fallback > 1))
      return make_unexpected(Error{ErrorCode::graph, "additive configuration is invalid"});
    if (const auto* layer = std::get_if<LayeredNodeConfig>(&node.config); layer) {
      if (layer->joint_weights.size() > max_joints || !std::isfinite(layer->fallback) ||
          layer->fallback < 0 || layer->fallback > 1 ||
          !std::ranges::all_of(layer->joint_weights, [](float value) {
            return std::isfinite(value) && value >= 0 && value <= 1;
          })) return make_unexpected(Error{ErrorCode::graph, "layer configuration is invalid"});
    }
    if (const auto* ik = std::get_if<TwoBoneIkNodeConfig>(&node.config); ik) {
      if (ik->root == ik->mid || ik->mid == ik->end || ik->root == ik->end ||
          ik->root.value >= max_joints || ik->mid.value >= max_joints ||
          ik->end.value >= max_joints || !finite(ik->pole) ||
          !std::ranges::all_of(ik->fallbacks, [](float value) { return std::isfinite(value); }) ||
          ik->fallbacks[3] < 0 || ik->fallbacks[3] > 1)
        return make_unexpected(Error{ErrorCode::graph, "IK node configuration is invalid"});
    }
    if (const auto* state = std::get_if<StateMachineNodeConfig>(&node.config); state) {
      const auto valid_state = validate_state_machine(state->definition);
      if (!valid_state || state->definition.states.size() != 2)
        return make_unexpected(Error{ErrorCode::graph, "state node requires a valid two-state definition"});
    }
  }
  const auto output_node = nodes.find(description.output->value);
  if (output_node == nodes.end() || output_node->second->type != NodeType::output) {
    return make_unexpected(Error{ErrorCode::graph, "output id does not identify Output node"});
  }

  std::map<std::pair<std::uint32_t, std::uint16_t>, std::uint32_t> incoming;
  std::map<std::uint32_t, std::vector<std::uint32_t>> outgoing;
  for (const auto& connection : description.connections) {
    const auto source = nodes.find(connection.source.value);
    const auto target = nodes.find(connection.target.value);
    if (source == nodes.end() || target == nodes.end() || connection.source_pin != 0 ||
        connection.type != PinType::pose ||
        connection.target_pin >= required_pose_inputs(target->second->type) ||
        !incoming.emplace(std::pair{connection.target.value, connection.target_pin},
                          connection.source.value).second) {
      return make_unexpected(Error{ErrorCode::graph, "invalid, typed, or duplicate connection"});
    }
    outgoing[connection.source.value].push_back(connection.target.value);
  }
  for (const auto& [id, node] : nodes) {
    for (std::uint16_t pin = 0; pin < required_pose_inputs(node->type); ++pin) {
      if (!incoming.contains({id, pin})) {
        return make_unexpected(Error{ErrorCode::graph, "required pose input is missing"});
      }
    }
  }

  std::map<std::uint32_t, std::size_t> indegree;
  for (const auto& [id, unused] : nodes) indegree[id] = 0;
  for (const auto& connection : description.connections) ++indegree[connection.target.value];
  std::set<std::uint32_t> ready;
  for (const auto& [id, degree] : indegree) if (degree == 0) ready.insert(id);
  std::vector<std::uint32_t> full_order;
  while (!ready.empty()) {
    const auto id = *ready.begin();
    ready.erase(ready.begin());
    full_order.push_back(id);
    for (const auto target : outgoing[id]) if (--indegree[target] == 0) ready.insert(target);
  }
  if (full_order.size() != nodes.size()) {
    return make_unexpected(Error{ErrorCode::graph, "graph contains a cycle"});
  }

  std::set<std::uint32_t> reachable;
  std::vector<std::uint32_t> pending{description.output->value};
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    if (!reachable.insert(id).second) continue;
    for (const auto& [target_pin, source] : incoming) {
      if (target_pin.first == id) pending.push_back(source);
    }
  }
  std::vector<std::uint32_t> order;
  for (const auto id : full_order) if (reachable.contains(id)) order.push_back(id);

  std::map<std::uint32_t, std::size_t> position;
  for (std::size_t index = 0; index < order.size(); ++index) position[order[index]] = index;
  std::map<std::uint32_t, std::size_t> last_use;
  for (std::size_t index = 0; index < order.size(); ++index) last_use[order[index]] = index;
  for (const auto& connection : description.connections) {
    if (reachable.contains(connection.source.value) && reachable.contains(connection.target.value)) {
      last_use[connection.source.value] = std::max(last_use[connection.source.value],
                                                   position[connection.target.value]);
    }
  }

  CompiledGraph graph;
  std::map<std::uint32_t, PoseSlot> node_slots;
  std::vector<std::size_t> slot_last_use;
  std::uint32_t state_cursor = 0;
  for (std::size_t index = 0; index < order.size(); ++index) {
    const auto id = order[index];
    const auto& node = *nodes[id];
    PoseSlot output_slot;
    if (!reuse_slots) {
      output_slot = PoseSlot{static_cast<std::uint32_t>(slot_last_use.size())};
      slot_last_use.push_back(last_use[id]);
    } else {
      auto free_slot = std::find_if(slot_last_use.begin(), slot_last_use.end(),
                                    [index](std::size_t owner_last) { return owner_last < index; });
      if (free_slot == slot_last_use.end()) {
        output_slot = PoseSlot{static_cast<std::uint32_t>(slot_last_use.size())};
        slot_last_use.push_back(last_use[id]);
      } else {
        output_slot = PoseSlot{static_cast<std::uint32_t>(free_slot - slot_last_use.begin())};
        *free_slot = last_use[id];
      }
    }
    node_slots[id] = output_slot;
    CompiledInstruction instruction;
    instruction.node = node.id;
    instruction.type = node.type;
    instruction.name = node.name;
    instruction.output = output_slot;
    instruction.state_size = node_state_size(node.type);
    instruction.state_offset = align_eight(state_cursor);
    state_cursor = instruction.state_offset + instruction.state_size;
    instruction.clip_index = node.clip_index;
    instruction.config = node.config;
    for (std::uint16_t pin = 0; pin < required_pose_inputs(node.type); ++pin) {
      instruction.inputs.push_back(node_slots.at(incoming.at({id, pin})));
    }
    graph.instructions.push_back(std::move(instruction));
  }
  graph.pose_slot_count = static_cast<std::uint32_t>(slot_last_use.size());
  graph.state_size = align_eight(state_cursor);
  graph.output_slot = node_slots.at(description.output->value);

  std::vector<GraphParameter> parameters = description.parameters;
  std::ranges::sort(parameters, {}, &GraphParameter::name);
  std::uint32_t runtime_parameter_index = 0;
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (parameters[index].name.empty() || !std::isfinite(parameters[index].default_value) ||
        (index > 0 && parameters[index - 1].name == parameters[index].name)) {
      return make_unexpected(Error{ErrorCode::graph, "graph parameter is invalid or duplicated"});
    }
    CompiledParameter compiled{parameters[index].name, 0, parameters[index].default_value,
                               parameters[index].constant};
    if (parameters[index].constant) {
      graph.constants.push_back(std::move(compiled));
    } else {
      compiled.offset = runtime_parameter_index * static_cast<std::uint32_t>(sizeof(float));
      ++runtime_parameter_index;
      graph.parameters.push_back(std::move(compiled));
    }
  }
  const auto resolve_parameter = [&graph](const std::string& name,
                                          float& fallback) -> Expected<std::uint32_t, Error> {
    if (name.empty()) return std::numeric_limits<std::uint32_t>::max();
    const auto runtime = std::ranges::find(graph.parameters, name, &CompiledParameter::name);
    if (runtime != graph.parameters.end())
      return static_cast<std::uint32_t>(runtime - graph.parameters.begin());
    const auto constant = std::ranges::find(graph.constants, name, &CompiledParameter::name);
    if (constant != graph.constants.end()) {
      fallback = constant->default_value;
      return std::numeric_limits<std::uint32_t>::max();
    }
    return make_unexpected(Error{ErrorCode::graph, "node references an unknown parameter"});
  };
  for (auto& instruction : graph.instructions) {
    auto bind = [&instruction, &resolve_parameter](const std::string& name, float& fallback) -> Expected<void, Error> {
      const auto index = resolve_parameter(name, fallback);
      if (!index) return make_unexpected(index.error());
      instruction.parameter_indices.push_back(*index);
      return {};
    };
    if (auto* blend1d = std::get_if<Blend1DNodeConfig>(&instruction.config)) {
      auto bound = bind(blend1d->parameter, blend1d->fallback); if (!bound) return make_unexpected(bound.error());
    } else if (auto* blend2d = std::get_if<Blend2DNodeConfig>(&instruction.config)) {
      for (std::size_t index = 0; index < 2; ++index) {
        auto bound = bind(blend2d->parameters[index], blend2d->fallbacks[index]);
        if (!bound) return make_unexpected(bound.error());
      }
    } else if (auto* additive = std::get_if<AdditiveNodeConfig>(&instruction.config)) {
      auto bound = bind(additive->parameter, additive->fallback); if (!bound) return make_unexpected(bound.error());
    } else if (auto* layer = std::get_if<LayeredNodeConfig>(&instruction.config)) {
      auto bound = bind(layer->parameter, layer->fallback); if (!bound) return make_unexpected(bound.error());
    } else if (auto* ik = std::get_if<TwoBoneIkNodeConfig>(&instruction.config)) {
      for (std::size_t index = 0; index < 4; ++index) {
        auto bound = bind(ik->parameters[index], ik->fallbacks[index]);
        if (!bound) return make_unexpected(bound.error());
      }
    } else if (const auto* state = std::get_if<StateMachineNodeConfig>(&instruction.config)) {
      for (const auto& transition : state->definition.transitions)
        if (transition.condition.parameter >= graph.parameters.size())
          return make_unexpected(Error{ErrorCode::graph, "state condition parameter is out of range"});
    }
  }
  graph.plan_json = canonical_plan_json(graph);
  graph.identity = fnv1a(graph.plan_json);
  return graph;
}

std::string canonical_plan_json(const CompiledGraph& graph) {
  std::ostringstream output;
  output << "{\"version\":" << graph.version << ",\"pose_slots\":" << graph.pose_slot_count
         << ",\"state_size\":" << graph.state_size << ",\"parameters\":[";
  for (std::size_t index = 0; index < graph.parameters.size(); ++index) {
    if (index) output << ',';
    const auto& parameter = graph.parameters[index];
    output << "{\"name\":\"" << escape_json(parameter.name) << "\",\"offset\":"
           << parameter.offset << ",\"default_bits\":"
           << std::bit_cast<std::uint32_t>(parameter.default_value) << ",\"constant\":"
           << (parameter.constant ? "true" : "false") << '}';
  }
  output << "],\"constants\":[";
  for (std::size_t index = 0; index < graph.constants.size(); ++index) {
    if (index) output << ',';
    const auto& parameter = graph.constants[index];
    output << "{\"name\":\"" << escape_json(parameter.name) << "\",\"value_bits\":"
           << std::bit_cast<std::uint32_t>(parameter.default_value) << '}';
  }
  output << "],\"instructions\":[";
  for (std::size_t index = 0; index < graph.instructions.size(); ++index) {
    if (index) output << ',';
    const auto& instruction = graph.instructions[index];
    output << "{\"id\":" << instruction.node.value << ",\"type\":\""
           << node_type_name(instruction.type) << "\",\"name\":\""
           << escape_json(instruction.name) << "\",\"inputs\":[";
    for (std::size_t pin = 0; pin < instruction.inputs.size(); ++pin) {
      if (pin) output << ',';
      output << instruction.inputs[pin].value;
    }
    output << "],\"output\":" << instruction.output.value << ",\"state_offset\":"
           << instruction.state_offset << ",\"state_size\":" << instruction.state_size;
    if (instruction.clip_index) output << ",\"clip\":" << *instruction.clip_index;
    output << ",\"parameter_indices\":[";
    for (std::size_t parameter = 0; parameter < instruction.parameter_indices.size(); ++parameter) {
      if (parameter) output << ',';
      output << instruction.parameter_indices[parameter];
    }
    output << ']';
    write_config(output, instruction.config);
    output << '}';
  }
  output << "]}";
  return output.str();
}

std::uint64_t plan_identity(const CompiledGraph& graph) noexcept {
  return graph.identity != 0 ? graph.identity : fnv1a(graph.plan_json);
}

}  // namespace animgraph
