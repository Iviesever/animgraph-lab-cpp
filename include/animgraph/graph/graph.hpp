#pragma once

#include "animgraph/core/expected.hpp"
#include "animgraph/core/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace animgraph {

struct NodeId {
  std::uint32_t value{};
  auto operator<=>(const NodeId&) const = default;
};

struct PoseSlot {
  std::uint32_t value{};
  auto operator<=>(const PoseSlot&) const = default;
};

enum class PinType : std::uint8_t { pose, scalar, vector2 };

struct PosePin { NodeId node; std::uint16_t index{}; };
struct ValuePin { NodeId node; std::uint16_t index{}; };

enum class NodeType : std::uint8_t {
  reference_pose,
  clip_player,
  blend_1d,
  blend_2d,
  additive,
  layered_blend_per_bone,
  pose_cache,
  two_bone_ik,
  state_machine,
  output
};

struct GraphParameter {
  std::string name;
  float default_value{};
  bool constant{};
};

struct NodeDefinition {
  NodeId id;
  NodeType type;
  std::string name;
  std::optional<std::size_t> clip_index;
};

struct Connection {
  NodeId source;
  std::uint16_t source_pin{};
  NodeId target;
  std::uint16_t target_pin{};
  PinType type{PinType::pose};
};

struct GraphDescription {
  std::vector<NodeDefinition> nodes;
  std::vector<Connection> connections;
  std::vector<GraphParameter> parameters;
  std::optional<NodeId> output;
};

class GraphBuilder {
 public:
  [[nodiscard]] NodeId add_node(NodeType type, std::string name);
  [[nodiscard]] Expected<void, Error> connect(PosePin source, PosePin target);
  [[nodiscard]] Expected<void, Error> connect(ValuePin source, ValuePin target);
  [[nodiscard]] Expected<void, Error> set_clip(NodeId node, std::size_t clip_index);
  void set_output(NodeId output) noexcept;
  void add_parameter(GraphParameter parameter);
  [[nodiscard]] GraphDescription build() const;

 private:
  GraphDescription description_;
};

struct CompiledParameter {
  std::string name;
  std::uint32_t offset{};
  float default_value{};
  bool constant{};
};

struct CompiledInstruction {
  NodeId node;
  NodeType type;
  std::string name;
  std::vector<PoseSlot> inputs;
  PoseSlot output;
  std::uint32_t state_offset{};
  std::uint32_t state_size{};
  std::optional<std::size_t> clip_index;
};

struct CompiledGraph {
  std::uint32_t version{1};
  std::vector<CompiledInstruction> instructions;
  std::vector<CompiledParameter> parameters;
  std::vector<CompiledParameter> constants;
  std::uint32_t pose_slot_count{};
  std::uint32_t state_size{};
  PoseSlot output_slot;
  std::string plan_json;
  std::uint64_t identity{};
};

[[nodiscard]] Expected<CompiledGraph, Error> compile_graph(
    const GraphDescription& description, bool reuse_slots = true);
[[nodiscard]] std::string canonical_plan_json(const CompiledGraph& graph);
[[nodiscard]] std::uint64_t plan_identity(const CompiledGraph& graph) noexcept;
[[nodiscard]] std::string_view node_type_name(NodeType type) noexcept;

}  // namespace animgraph
