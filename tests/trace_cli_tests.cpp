#include "animgraph/app/lab.hpp"
#include "animgraph/samples/procedural.hpp"
#include "animgraph/trace/trace.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace {

using namespace animgraph;

ANIMGRAPH_TEST(procedural_demo_contains_humanoid_clips_and_all_p0_graph_nodes) {
  const auto demo = make_locomotion_demo();
  AG_CHECK(demo.has_value());
  AG_CHECK(demo->skeleton.joints.size() >= 12U);
  const std::array names{std::string{"Idle"}, std::string{"Walk"}, std::string{"Run"},
                         std::string{"Turn"}, std::string{"AimAdditive"},
                         std::string{"UpperBodyLayer"}};
  for (const auto& name : names) {
    AG_CHECK(std::ranges::find(demo->clips, name, &AnimationClip::name) != demo->clips.end());
  }
  std::array<bool, 10> present{};
  for (const auto& instruction : demo->graph.instructions)
    present[static_cast<std::size_t>(instruction.type)] = true;
  for (const bool value : present) AG_CHECK(value);
}

ANIMGRAPH_TEST(trace_serializes_real_runtime_fields_and_viewer_is_self_contained) {
  auto demo = make_locomotion_demo().value();
  const auto trace = generate_demo_trace(demo, 12, AnimTime{800}, "test-sha");
  AG_CHECK(trace.has_value());
  AG_CHECK_EQ(trace->frames.size(), 12U);
  const std::string json = trace_to_json(*trace);
  for (const std::string_view field : {"\"skeleton\"", "\"local_pose\"", "\"model_pose\"",
       "\"graph_plan\"", "\"state\"", "\"transition\"", "\"blend_weights\"",
       "\"pose_cache\"", "\"events\"", "\"sync_markers\"", "\"root_motion\"",
       "\"ik\"", "\"compression\"", "\"evaluation_us\"", "\"success\":true"}) {
    AG_CHECK(json.find(field) != std::string::npos);
  }
  const std::string html = generate_viewer_html(json);
  for (const std::string_view control : {"playPause", "step", "timeline", "spaceMode",
                                         "skeletonCanvas", "rootCanvas", "graphNodes"}) {
    AG_CHECK(html.find(control) != std::string::npos);
  }
  AG_CHECK(html.find("https://") == std::string::npos);
  AG_CHECK(html.find("http://") == std::string::npos);
  AG_CHECK(html.find(json) != std::string::npos);
}

ANIMGRAPH_TEST(benchmark_covers_required_character_counts_and_scenarios) {
  auto demo = make_locomotion_demo().value();
  const auto report = run_benchmark_matrix(demo);
  AG_CHECK(report.has_value());
  for (const std::size_t count : {1U, 100U, 1000U}) {
    AG_CHECK(std::ranges::find(report->rows, count, &BenchmarkRow::characters) != report->rows.end());
  }
  for (const std::string_view scenario : {"clip_only", "complex_graph", "state_transition",
       "ik_enabled", "compressed_vs_raw", "serial", "batch_parallel"}) {
    AG_CHECK(std::ranges::find(report->rows, scenario, &BenchmarkRow::scenario) != report->rows.end());
  }
  for (const auto& row : report->rows) {
    AG_CHECK(row.total_nanoseconds > 0U);
    AG_CHECK(row.nanoseconds_per_character > 0.0);
  }
  const auto clip_only = std::ranges::find(report->rows, std::string_view{"clip_only"},
                                           &BenchmarkRow::scenario);
  AG_CHECK(clip_only != report->rows.end());
  AG_CHECK_EQ(clip_only->nodes, 0U);
  const auto compression = std::ranges::find(report->rows, std::string_view{"compressed_vs_raw"},
                                              &BenchmarkRow::scenario);
  AG_CHECK(compression != report->rows.end());
  AG_CHECK(compression->compressed_bytes < compression->raw_bytes);
}

ANIMGRAPH_TEST(cli_evaluate_viewer_benchmark_asset_and_verify_commands_work) {
  namespace fs = std::filesystem;
  const fs::path directory = fs::path{"artifacts"} / "tests" / "cli";
  fs::create_directories(directory);
  const std::string trace_path = (directory / "trace.json").string();
  const std::string viewer_path = (directory / "viewer.html").string();
  const std::string benchmark_path = (directory / "benchmark.json").string();
  const std::string asset_path = (directory / "sample.agclip").string();
  std::ostringstream output, error;

  const std::array evaluate_args{std::string_view{"evaluate"}, std::string_view{"--sample"},
      std::string_view{"locomotion"}, std::string_view{"--trace"}, std::string_view{trace_path},
      std::string_view{"--git-sha"}, std::string_view{"test-sha"}};
  AG_CHECK_EQ(run_lab(evaluate_args, output, error), 0);
  AG_CHECK(fs::file_size(trace_path) > 1000U);
  std::ifstream trace_input(trace_path, std::ios::binary);
  const std::string trace_text((std::istreambuf_iterator<char>(trace_input)),
                               std::istreambuf_iterator<char>());
  AG_CHECK(trace_text.find("\"git_sha\":\"test-sha\"") != std::string::npos);
  const std::array viewer_args{std::string_view{"generate-viewer"}, std::string_view{"--trace"},
      std::string_view{trace_path}, std::string_view{"--out"}, std::string_view{viewer_path}};
  AG_CHECK_EQ(run_lab(viewer_args, output, error), 0);
  AG_CHECK(fs::file_size(viewer_path) > fs::file_size(trace_path));
  const std::array benchmark_args{std::string_view{"benchmark"}, std::string_view{"--out"},
      std::string_view{benchmark_path}};
  AG_CHECK_EQ(run_lab(benchmark_args, output, error), 0);
  AG_CHECK(fs::file_size(benchmark_path) > 500U);
  const std::array compile_args{std::string_view{"compile-asset"}, std::string_view{"clip"},
      std::string_view{asset_path}};
  AG_CHECK_EQ(run_lab(compile_args, output, error), 0);
  const std::array inspect_args{std::string_view{"inspect-asset"}, std::string_view{asset_path}};
  AG_CHECK_EQ(run_lab(inspect_args, output, error), 0);
  const std::array sample_args{std::string_view{"sample"}};
  AG_CHECK_EQ(run_lab(sample_args, output, error), 0);
  const std::array verify_args{std::string_view{"verify"}};
  AG_CHECK_EQ(run_lab(verify_args, output, error), 0);
}

}  // namespace
