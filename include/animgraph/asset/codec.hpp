#pragma once

#include "animgraph/clip/clip.hpp"
#include "animgraph/core/expected.hpp"
#include "animgraph/graph/graph.hpp"
#include "animgraph/skeleton/skeleton.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace animgraph {

inline constexpr std::uint16_t skeleton_asset_version = 1;
inline constexpr std::uint16_t clip_asset_version = 1;
inline constexpr std::uint16_t graph_asset_version = 1;

enum class AssetKind { skeleton, clip, graph };

struct AssetSummary {
  AssetKind kind;
  std::uint16_t version;
  std::uint32_t primary_count;
  std::size_t byte_size;
};

[[nodiscard]] Expected<std::vector<std::byte>, Error> encode_skeleton(
    const CompiledSkeleton& skeleton);
[[nodiscard]] Expected<CompiledSkeleton, Error> decode_skeleton(
    std::span<const std::byte> bytes);
[[nodiscard]] Expected<std::vector<std::byte>, Error> encode_clip(
    const AnimationClip& clip);
[[nodiscard]] Expected<AnimationClip, Error> decode_clip(
    std::span<const std::byte> bytes);
[[nodiscard]] Expected<std::vector<std::byte>, Error> encode_graph_plan(
    const CompiledGraph& graph);
[[nodiscard]] Expected<std::string, Error> decode_graph_plan(
    std::span<const std::byte> bytes);
[[nodiscard]] Expected<AssetSummary, Error> inspect_asset(
    std::span<const std::byte> bytes);

}  // namespace animgraph
