#pragma once

#include "animgraph/clip/clip.hpp"
#include "animgraph/core/expected.hpp"

#include <cstddef>
#include <vector>

namespace animgraph {

struct CompressionSettings {
  float translation_error{0.001F};
  float rotation_radians_error{0.001F};
  float scale_error{0.001F};
};

struct TrackCompressionReport {
  JointId joint;
  std::size_t raw_keys{};
  std::size_t compressed_keys{};
  std::size_t raw_bytes{};
  std::size_t compressed_bytes{};
  float max_translation_error{};
  float max_rotation_error{};
  float max_scale_error{};
};

struct CompressionReport {
  std::size_t raw_keys{};
  std::size_t compressed_keys{};
  std::size_t raw_bytes{};
  std::size_t compressed_bytes{};
  float max_translation_error{};
  float max_rotation_error{};
  float max_scale_error{};
  std::vector<TrackCompressionReport> tracks;
};

struct CompressedClip {
  AnimationClip clip;
};

[[nodiscard]] Expected<CompressedClip, Error> compress_clip(
    const AnimationClip& source, const CompressionSettings& settings,
    CompressionReport& report);
[[nodiscard]] AnimationClip decompress_clip(const CompressedClip& compressed);

}  // namespace animgraph
