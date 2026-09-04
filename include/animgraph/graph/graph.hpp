#pragma once

#include "animgraph/core/expected.hpp"
#include "animgraph/core/types.hpp"
#include "animgraph/ik/two_bone_ik.hpp"
#include "animgraph/runtime/state_machine.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <variant>

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
struct ValuePin {
  std::optional<NodeId> node;
  std::uint16_t index{};
  std::string parameter;
  explicit ValuePin(NodeId target, std::uint16_t pin = 0) : node(target), index(pin) {}
  explicit ValuePin(std::string parameter_name) : parameter(std::move(parameter_name)) {}
};

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

struct GraphPoint2 { float x{}; float y{}; };
struct ClipPlayerNodeConfig { JointId root_motion_joint{0}; bool remove_root_motion{}; };
struct Blend1DNodeConfig {
  std::array<float, 2> thresholds{0.0F, 1.0F};
  std::string parameter;
  float fallback{0.5F};
};
struct Blend2DNodeConfig {
  std::array<GraphPoint2, 3> points{GraphPoint2{0,0}, GraphPoint2{1,0}, GraphPoint2{0,1}};
  std::array<std::string, 2> parameters;
  std::array<float, 2> fallbacks{0.0F, 0.0F};
};
struct AdditiveNodeConfig { std::string parameter; float fallback{1.0F}; };
struct LayeredNodeConfig {
  std::vector<float> joint_weights;
  std::string parameter;
  float fallback{1.0F};
};
struct TwoBoneIkNodeConfig {
  JointId root{0};
  JointId mid{1};
  JointId end{2};
  Vec3 pole{0,0,1};
  std::array<std::string, 4> parameters;
  std::array<float, 4> fallbacks{0.0F, 0.0F, 0.0F, 1.0F};
  std::optional<JointLimit> limit;
};
struct StateMachineNodeConfig {
  StateMachineDefinition definition;
  std::array<std::optional<std::size_t>, 2> sync_clip_indices;
};
using NodeConfig = std::variant<std::monostate, ClipPlayerNodeConfig,
    Blend1DNodeConfig, Blend2DNodeConfig, AdditiveNodeConfig, LayeredNodeConfig,
    TwoBoneIkNodeConfig, StateMachineNodeConfig>;

struct NodeDefinition {
  NodeId id;
  NodeType type;
  std::string name;
  std::optional<std::size_t> clip_index;
  NodeConfig config;
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
  [[nodiscard]] Expected<void, Error> configure_clip_player(
      NodeId node, JointId root_motion_joint, bool remove_root_motion);
  [[nodiscard]] Expected<void, Error> configure_blend_1d(
      NodeId node, std::array<float, 2> thresholds, std::string parameter,
      float fallback = 0.5F);
  [[nodiscard]] Expected<void, Error> configure_blend_2d(
      NodeId node, std::array<GraphPoint2, 3> points,
      std::array<std::string, 2> parameters,
      std::array<float, 2> fallbacks = {0.0F, 0.0F});
  [[nodiscard]] Expected<void, Error> configure_additive(
      NodeId node, std::string parameter, float fallback = 1.0F);
  [[nodiscard]] Expected<void, Error> configure_layered(
      NodeId node, std::vector<float> joint_weights, std::string parameter,
      float fallback = 1.0F);
  [[nodiscard]] Expected<void, Error> configure_two_bone_ik(
      NodeId node, TwoBoneIkNodeConfig config);
  [[nodiscard]] Expected<void, Error> configure_state_machine(
      NodeId node, StateMachineNodeConfig config);
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
  NodeConfig config;
  std::vector<std::uint32_t> parameter_indices;
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
