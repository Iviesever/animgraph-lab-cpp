#include "animgraph/asset/tool.hpp"

#include "animgraph/asset/codec.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace animgraph {
namespace {

Expected<std::vector<std::byte>, Error> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return make_unexpected(Error{ErrorCode::invalid_argument, "cannot open input file"});
  const auto end = input.tellg();
  if (end < 0 || static_cast<std::uint64_t>(end) > max_asset_bytes) {
    return make_unexpected(Error{ErrorCode::bounds, "input file exceeds asset limit"});
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  input.seekg(0);
  if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), end);
  if (!input) return make_unexpected(Error{ErrorCode::invalid_format, "failed to read complete file"});
  return bytes;
}

Expected<void, Error> write_file(const std::filesystem::path& path,
                                 std::span<const std::byte> bytes) {
  if (bytes.size() > max_asset_bytes) return make_unexpected(Error{ErrorCode::bounds, "asset exceeds limit"});
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return make_unexpected(Error{ErrorCode::invalid_argument, "cannot open output file"});
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  if (!output) return make_unexpected(Error{ErrorCode::invalid_format, "failed to write complete file"});
  return {};
}

Expected<CompiledSkeleton, Error> sample_skeleton() {
  RawSkeleton raw{{RawJoint{.name = "root", .parent = std::nullopt,
                            .reference_local = Transform::identity(),
                            .inverse_bind = Transform::identity(), .semantic = "root"},
                   RawJoint{.name = "hand", .parent = 0,
                            .reference_local = Transform{Vec3{0, 1, 0}, Quat::identity(), Vec3{1, 1, 1}},
                            .inverse_bind = Transform::identity(), .semantic = "hand"}}};
  return compile_skeleton(raw);
}

AnimationClip sample_animation() {
  AnimationClip clip;
  clip.name = "sample";
  clip.duration = AnimTime{AnimTime::ticks_per_second};
  clip.mode = ClipPlaybackMode::loop;
  JointTrack root;
  root.joint = JointId{0};
  root.translations = {{AnimTime{0}, Vec3{0, 0, 0}},
                       {clip.duration, Vec3{1, 0, 0}}};
  root.rotations = {{AnimTime{0}, Quat::identity()}};
  root.scales = {{AnimTime{0}, Vec3{1, 1, 1}}};
  clip.tracks.push_back(std::move(root));
  clip.events.push_back({AnimTime{AnimTime::ticks_per_second / 2}, "sample_event", 1});
  clip.markers.push_back({AnimTime{0}, "start"});
  return clip;
}

void usage(std::ostream& output) {
  output << "Usage:\n"
            "  animc compile <skeleton|clip> <output>\n"
            "  animc inspect <asset>\n"
            "  animc validate <asset>\n";
}

}  // namespace

int run_asset_tool(std::span<const std::string_view> arguments,
                   std::ostream& output, std::ostream& error) {
  if (arguments.empty()) {
    usage(error);
    return 2;
  }
  if (arguments[0] == "compile") {
    if (arguments.size() != 3) { usage(error); return 2; }
    Expected<std::vector<std::byte>, Error> encoded =
        make_unexpected(Error{ErrorCode::unsupported, "unknown sample asset type"});
    if (arguments[1] == "skeleton") {
      const auto skeleton = sample_skeleton();
      if (!skeleton) { error << skeleton.error().message << '\n'; return 1; }
      encoded = encode_skeleton(*skeleton);
    } else if (arguments[1] == "clip") {
      encoded = encode_clip(sample_animation());
    }
    if (!encoded) { error << encoded.error().message << '\n'; return 1; }
    const auto written = write_file(std::filesystem::path{arguments[2]}, *encoded);
    if (!written) { error << written.error().message << '\n'; return 1; }
    output << "wrote " << encoded->size() << " bytes\n";
    return 0;
  }
  if (arguments[0] == "inspect" || arguments[0] == "validate") {
    if (arguments.size() != 2) { usage(error); return 2; }
    const auto bytes = read_file(std::filesystem::path{arguments[1]});
    if (!bytes) { error << bytes.error().message << '\n'; return 1; }
    const auto summary = inspect_asset(*bytes);
    if (!summary) { error << summary.error().message << '\n'; return 1; }
    if (arguments[0] == "validate") {
      output << "valid\n";
    } else {
      output << "{\"kind\":\"" << (summary->kind == AssetKind::skeleton ? "skeleton" : "clip")
             << "\",\"version\":" << summary->version << ",\"count\":"
             << summary->primary_count << ",\"bytes\":" << summary->byte_size << "}\n";
    }
    return 0;
  }
  usage(error);
  return 2;
}

}  // namespace animgraph
