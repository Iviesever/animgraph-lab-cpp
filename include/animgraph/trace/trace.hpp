#pragma once

#include "animgraph/core/expected.hpp"
#include "animgraph/samples/procedural.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace animgraph {

struct TraceClipTime { std::string node; std::string clip; AnimTime time; };
struct TraceFrame {
  std::uint32_t index{};
  AnimTime time;
  LocalPose local_pose;
  ModelPose model_pose;
  std::string current_node;
  std::string state;
  float transition{};
  std::vector<TraceClipTime> clip_times;
  std::vector<float> blend_weights;
  std::uint32_t pose_cache_hits{};
  std::uint32_t pose_cache_misses{};
  std::vector<std::string> events;
  std::vector<std::string> sync_markers;
  Transform root_motion{Transform::identity()};
  Transform root_accumulated{Transform::identity()};
  bool ik_applied{};
  Vec3 ik_target{};
  float ik_error{};
  double evaluation_microseconds{};
  bool success{};
};
struct TraceDocument {
  std::uint32_t version{1};
  std::string git_sha;
  std::uint16_t asset_version{1};
  std::uint32_t graph_plan_version{1};
  CompiledSkeleton skeleton;
  CompiledGraph graph;
  CompressionReport compression;
  std::vector<TraceFrame> frames;
};

struct BenchmarkRow {
  std::string scenario;
  std::size_t characters{};
  std::size_t joints{};
  std::size_t nodes{};
  std::size_t pose_evaluations{};
  std::uint64_t total_nanoseconds{};
  double nanoseconds_per_character{};
  double nanoseconds_per_joint{};
  double pose_cache_hit_rate{};
  std::size_t scratch_pose_slots{};
  std::size_t raw_bytes{};
  std::size_t compressed_bytes{};
  float max_translation_error{};
  float max_rotation_error{};
  float max_scale_error{};
  std::size_t worker_count{};
};
struct BenchmarkReport {
  std::string compiler;
  std::string operating_system;
  std::string cpu;
  std::vector<BenchmarkRow> rows;
};

[[nodiscard]] Expected<TraceDocument, Error> generate_demo_trace(
    DemoBundle& demo, std::size_t frame_count, AnimTime delta,
    std::string git_sha);
[[nodiscard]] std::string trace_to_json(const TraceDocument& trace);
[[nodiscard]] std::string generate_viewer_html(std::string_view trace_json);
[[nodiscard]] Expected<BenchmarkReport, Error> run_benchmark_matrix(DemoBundle& demo);
[[nodiscard]] std::string benchmark_to_json(const BenchmarkReport& report);

}  // namespace animgraph
