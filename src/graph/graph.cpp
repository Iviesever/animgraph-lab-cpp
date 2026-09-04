#include "animgraph/graph/graph.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
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

}  // namespace

NodeId GraphBuilder::add_node(NodeType type, std::string name) {
  const NodeId id{static_cast<std::uint32_t>(description_.nodes.size())};
  description_.nodes.push_back(NodeDefinition{.id = id, .type = type, .name = std::move(name),
                                               .clip_index = std::nullopt});
  return id;
}
Expected<void, Error> GraphBuilder::connect(PosePin source, PosePin target) {
  description_.connections.push_back(Connection{source.node, source.index, target.node,
                                                 target.index, PinType::pose});
  return {};
}
Expected<void, Error> GraphBuilder::connect(ValuePin source, ValuePin target) {
  description_.connections.push_back(Connection{source.node, source.index, target.node,
                                                 target.index, PinType::scalar});
  return {};
}
Expected<void, Error> GraphBuilder::set_clip(NodeId node, std::size_t clip_index) {
  const auto found = std::ranges::find(description_.nodes, node, &NodeDefinition::id);
  if (found == description_.nodes.end() || found->type != NodeType::clip_player) {
    return make_unexpected(Error{ErrorCode::graph, "clip binding targets a non-player node"});
  }
  found->clip_index = clip_index;
  return {};
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
    output << '}';
  }
  output << "]}";
  return output.str();
}

std::uint64_t plan_identity(const CompiledGraph& graph) noexcept {
  return graph.identity != 0 ? graph.identity : fnv1a(graph.plan_json);
}

}  // namespace animgraph
