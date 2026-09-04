#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace animgraph {

enum class ErrorCode {
  invalid_argument,
  non_finite,
  zero_length,
  bounds,
  hierarchy,
  size_mismatch,
  invalid_format,
  integrity,
  graph,
  unsupported,
  cancelled
};

struct Error {
  ErrorCode code{ErrorCode::invalid_argument};
  std::string message;
};

struct JointId {
  std::uint32_t value{std::numeric_limits<std::uint32_t>::max()};
  auto operator<=>(const JointId&) const = default;
};

inline constexpr std::size_t max_joints = 256;
inline constexpr std::size_t max_graph_nodes = 4096;
inline constexpr std::size_t max_track_keys = 16384;
inline constexpr std::size_t max_events = 4096;
inline constexpr std::size_t max_asset_bytes = 64U * 1024U * 1024U;

}  // namespace animgraph
