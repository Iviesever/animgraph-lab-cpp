#include "animgraph/app/lab.hpp"

#include "animgraph/asset/tool.hpp"
#include "animgraph/core/types.hpp"
#include "animgraph/core/version.hpp"
#include "animgraph/samples/procedural.hpp"
#include "animgraph/trace/trace.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace animgraph {
namespace {

Expected<std::string, Error> read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return make_unexpected(Error{ErrorCode::invalid_argument, "cannot open input file"});
  const auto end = input.tellg();
  if (end < 0 || static_cast<std::uint64_t>(end) > max_asset_bytes)
    return make_unexpected(Error{ErrorCode::bounds, "input text exceeds 64 MiB"});
  std::string text(static_cast<std::size_t>(end), '\0');
  input.seekg(0);
  if (!text.empty()) input.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!input) return make_unexpected(Error{ErrorCode::invalid_format, "failed to read complete text"});
  return text;
}

Expected<void, Error> write_text(const std::filesystem::path& path, std::string_view text) {
  if (text.size() > max_asset_bytes)
    return make_unexpected(Error{ErrorCode::bounds, "output text exceeds 64 MiB"});
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return make_unexpected(Error{ErrorCode::invalid_argument, "cannot open output file"});
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!output) return make_unexpected(Error{ErrorCode::invalid_format, "failed to write complete text"});
  return {};
}

std::optional<std::string_view> option(std::span<const std::string_view> arguments,
                                       std::string_view name) {
  for (std::size_t index = 0; index + 1 < arguments.size(); ++index)
    if (arguments[index] == name) return arguments[index + 1];
  return std::nullopt;
}

void usage(std::ostream& output) {
  output << "Usage: animgraph_lab <command>\n"
            "Commands:\n"
            "  sample\n"
            "  evaluate --sample locomotion --trace <trace.json> [--git-sha <sha>]\n"
            "  benchmark --out <benchmark.json>\n"
            "  compile-asset <skeleton|clip> <output>\n"
            "  inspect-asset <asset>\n"
            "  generate-viewer --trace <trace.json> --out <viewer.html>\n"
            "  verify\n"
            "  --version\n";
}

}  // namespace

int run_lab(std::span<const std::string_view> arguments,
            std::ostream& output, std::ostream& error) {
  if (arguments.empty()) { usage(error); return 2; }
  const auto command = arguments.front();
  if (command == "--version") { output << version_string() << '\n'; return 0; }
  if (command == "compile-asset") {
    if (arguments.size() != 3) { usage(error); return 2; }
    const std::array forwarded{std::string_view{"compile"}, arguments[1], arguments[2]};
    return run_asset_tool(forwarded, output, error);
  }
  if (command == "inspect-asset") {
    if (arguments.size() != 2) { usage(error); return 2; }
    const std::array forwarded{std::string_view{"inspect"}, arguments[1]};
    return run_asset_tool(forwarded, output, error);
  }
  if (command == "generate-viewer") {
    const auto trace_path = option(arguments, "--trace");
    const auto output_path = option(arguments, "--out");
    if (!trace_path || !output_path) { usage(error); return 2; }
    const auto trace = read_text(std::filesystem::path{*trace_path});
    if (!trace) { error << trace.error().message << '\n'; return 1; }
    const auto written = write_text(std::filesystem::path{*output_path}, generate_viewer_html(*trace));
    if (!written) { error << written.error().message << '\n'; return 1; }
    output << "viewer=" << *output_path << '\n';
    return 0;
  }
  auto demo = make_locomotion_demo();
  if (!demo) { error << demo.error().message << '\n'; return 1; }
  if (command == "sample") {
    output << "{\"sample\":\"locomotion\",\"joints\":" << demo->skeleton.joints.size()
           << ",\"clips\":" << demo->clips.size() << ",\"nodes\":"
           << demo->graph.instructions.size() << "}\n";
    return 0;
  }
  if (command == "evaluate") {
    const auto sample = option(arguments, "--sample");
    const auto trace_path = option(arguments, "--trace");
    if (!sample || *sample != "locomotion" || !trace_path) { usage(error); return 2; }
    const auto git_sha = option(arguments, "--git-sha").value_or("unbound-source");
    auto trace = generate_demo_trace(*demo, 60, AnimTime{800}, std::string{git_sha});
    if (!trace) { error << trace.error().message << '\n'; return 1; }
    const auto written = write_text(std::filesystem::path{*trace_path}, trace_to_json(*trace));
    if (!written) { error << written.error().message << '\n'; return 1; }
    output << "trace=" << *trace_path << " frames=" << trace->frames.size() << '\n';
    return 0;
  }
  if (command == "benchmark") {
    const auto output_path = option(arguments, "--out");
    if (!output_path) { usage(error); return 2; }
    const auto report = run_benchmark_matrix(*demo);
    if (!report) { error << report.error().message << '\n'; return 1; }
    const auto written = write_text(std::filesystem::path{*output_path}, benchmark_to_json(*report));
    if (!written) { error << written.error().message << '\n'; return 1; }
    output << "benchmark=" << *output_path << " rows=" << report->rows.size() << '\n';
    return 0;
  }
  if (command == "verify") {
    const auto trace = generate_demo_trace(*demo, 4, AnimTime{800}, "working-tree");
    if (!trace || trace->frames.size() != 4 || demo->graph.instructions.size() < 10) {
      error << "runtime verification failed\n";
      return 1;
    }
    output << "{\"success\":true,\"version\":\"" << version_string()
           << "\",\"tests\":\"runtime-smoke\",\"nodes\":"
           << demo->graph.instructions.size() << "}\n";
    return 0;
  }
  usage(error);
  return 2;
}

}  // namespace animgraph
