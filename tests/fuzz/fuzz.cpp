#include "animgraph/asset/codec.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using namespace animgraph;

std::uint32_t next(std::uint32_t& state) {
  state = state * 1'664'525U + 1'013'904'223U;
  return state;
}
void patch_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  if (offset + 4 > bytes.size()) return;
  for (unsigned byte = 0; byte < 4; ++byte)
    bytes[offset + byte] = static_cast<std::byte>((value >> (byte * 8U)) & 0xFFU);
}
std::uint32_t crc32(const std::vector<std::byte>& bytes, std::size_t zero_offset) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto input = index >= zero_offset && index < zero_offset + 4
        ? std::uint8_t{0} : std::to_integer<std::uint8_t>(bytes[index]);
    crc ^= input;
    for (int bit = 0; bit < 8; ++bit) {
      const std::uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}
void refresh_crc(std::vector<std::byte>& bytes, std::size_t offset) {
  patch_u32(bytes, offset, 0);
  patch_u32(bytes, offset, crc32(bytes, offset));
}

CompiledSkeleton skeleton() {
  return compile_skeleton(RawSkeleton{{RawJoint{.name = "root", .parent = std::nullopt,
      .reference_local = Transform::identity(), .inverse_bind = Transform::identity(),
      .semantic = "root"}}}).value();
}
AnimationClip clip() {
  AnimationClip value;
  value.name = "fuzz"; value.duration = AnimTime{100}; value.mode = ClipPlaybackMode::loop;
  JointTrack track; track.joint = JointId{0};
  track.translations = {{AnimTime{0}, Vec3{}}, {AnimTime{100}, Vec3{1, 0, 0}}};
  value.tracks.push_back(std::move(track));
  return value;
}
CompiledGraph graph() {
  GraphBuilder builder;
  const auto reference = builder.add_node(NodeType::reference_pose, "reference");
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{reference, 0}, PosePin{output, 0}).value();
  builder.set_output(output);
  return compile_graph(builder.build()).value();
}

}  // namespace

int main() {
  const auto skeleton_bytes = encode_skeleton(skeleton()).value();
  const auto clip_bytes = encode_clip(clip()).value();
  const auto graph_bytes = encode_graph_plan(graph()).value();
  std::uint32_t state = 0xF022CAFEU;
  for (std::size_t iteration = 0; iteration < 100'000; ++iteration) {
    std::vector<std::byte> bytes;
    switch (iteration % 8U) {
      case 0:
        bytes.resize(next(state) % 1024U);
        for (auto& byte : bytes) byte = static_cast<std::byte>(next(state) >> 24U);
        break;
      case 1:
        bytes = skeleton_bytes;
        bytes.resize(next(state) % bytes.size());
        break;
      case 2:
        bytes = skeleton_bytes;
        patch_u32(bytes, 24, 0xFFFFFFFFU);
        refresh_crc(bytes, 36);
        break;
      case 3:
        bytes = clip_bytes;
        patch_u32(bytes, 24, 0xFFFFFFFFU);
        refresh_crc(bytes, 76);
        break;
      case 4:
        bytes = graph_bytes;
        patch_u32(bytes, 28, 0xFFFFFFFFU);
        refresh_crc(bytes, 32);
        break;
      case 5:
        bytes = skeleton_bytes;
        patch_u32(bytes, 64, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
        refresh_crc(bytes, 36);
        break;
      case 6:
        bytes = clip_bytes;
        patch_u32(bytes, 20, 0x7FFFFFFFU);
        refresh_crc(bytes, 76);
        break;
      default:
        bytes = graph_bytes;
        bytes.back() ^= static_cast<std::byte>(next(state) & 0xFFU);
        refresh_crc(bytes, 32);
        break;
    }
    static_cast<void>(decode_skeleton(bytes));
    static_cast<void>(decode_clip(bytes));
    static_cast<void>(decode_graph_plan(bytes));
    static_cast<void>(inspect_asset(bytes));
  }
  std::cout << "FUZZ inputs=100000 max_bytes=1024 deep_crc=true formats=3\n";
  return 0;
}
