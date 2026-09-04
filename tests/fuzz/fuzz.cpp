#include "animgraph/asset/codec.hpp"

#include <array>
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
  std::size_t verified_valid = 0, verified_invalid = 0, random_inputs = 0;
  for (std::size_t iteration = 0; iteration < 100'000; ++iteration) {
    std::vector<std::byte> bytes;
    enum class ExpectedKind { none, skeleton, clip, graph };
    ExpectedKind expected = ExpectedKind::none;
    bool must_fail = false;
    switch (iteration % 12U) {
      case 0:
        bytes.resize(next(state) % 1024U);
        for (auto& byte : bytes) byte = static_cast<std::byte>(next(state) >> 24U);
        ++random_inputs;
        break;
      case 1:
        bytes = skeleton_bytes;
        bytes.resize(next(state) % bytes.size());
        expected = ExpectedKind::skeleton; must_fail = true;
        break;
      case 2:
        bytes = clip_bytes;
        bytes.resize(next(state) % bytes.size());
        expected = ExpectedKind::clip; must_fail = true;
        break;
      case 3:
        bytes = graph_bytes;
        bytes.resize(next(state) % bytes.size());
        expected = ExpectedKind::graph; must_fail = true;
        break;
      case 4:
        bytes = skeleton_bytes;
        patch_u32(bytes, 24, 257U + next(state) % 100'000U);
        refresh_crc(bytes, 36);
        expected = ExpectedKind::skeleton; must_fail = true;
        break;
      case 5:
        bytes = clip_bytes;
        patch_u32(bytes, 20, 1024U + next(state));
        refresh_crc(bytes, 76);
        expected = ExpectedKind::clip; must_fail = true;
        break;
      case 6:
        bytes = graph_bytes;
        for (std::size_t index = 36; index < bytes.size(); ++index)
          bytes[index] = static_cast<std::byte>('a' + next(state) % 26U);
        bytes[36] = std::byte{'{'}; bytes.back() = std::byte{'}'};
        refresh_crc(bytes, 32);
        expected = ExpectedKind::graph; must_fail = true;
        break;
      case 7: {
        bytes = skeleton_bytes;
        const std::array non_finite{
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()};
        patch_u32(bytes, 64, std::bit_cast<std::uint32_t>(non_finite[next(state) % non_finite.size()]));
        refresh_crc(bytes, 36);
        expected = ExpectedKind::skeleton; must_fail = true;
        break;
      }
      case 8: {
        bytes = clip_bytes;
        const float invalid = next(state) & 1U ? std::numeric_limits<float>::quiet_NaN()
                                               : std::numeric_limits<float>::infinity();
        patch_u32(bytes, 116, std::bit_cast<std::uint32_t>(invalid));
        refresh_crc(bytes, 76);
        expected = ExpectedKind::clip; must_fail = true;
        break;
      }
      case 9:
        if (next(state) % 2U) { bytes = skeleton_bytes; expected = ExpectedKind::skeleton; }
        else { bytes = clip_bytes; expected = ExpectedKind::clip; }
        bytes.back() ^= static_cast<std::byte>(next(state) & 0xFFU);
        if (bytes.back() == (expected == ExpectedKind::skeleton ? skeleton_bytes.back() : clip_bytes.back()))
          bytes.back() ^= std::byte{1};
        must_fail = true;
        break;
      case 10:
        switch (next(state) % 3U) {
          case 0: bytes = skeleton_bytes; expected = ExpectedKind::skeleton; break;
          case 1: bytes = clip_bytes; expected = ExpectedKind::clip; break;
          default: bytes = graph_bytes; expected = ExpectedKind::graph; break;
        }
        break;
      default:
        bytes = graph_bytes;
        patch_u32(bytes, 28, 2U + next(state) % 1024U);
        refresh_crc(bytes, 32);
        expected = ExpectedKind::graph; must_fail = true;
        break;
    }
    const auto decoded_skeleton = decode_skeleton(bytes);
    const auto decoded_clip = decode_clip(bytes);
    const auto decoded_graph = decode_graph_plan(bytes);
    const auto inspected = inspect_asset(bytes);
    if (must_fail) {
      if (inspected) return 1;
      ++verified_invalid;
    } else if (expected != ExpectedKind::none) {
      const bool valid = expected == ExpectedKind::skeleton ? decoded_skeleton.has_value()
          : expected == ExpectedKind::clip ? decoded_clip.has_value() : decoded_graph.has_value();
      if (!valid || !inspected) return 1;
      ++verified_valid;
    }
  }
  std::cout << "FUZZ inputs=100000 max_bytes=1024 deep_crc=true formats=3 verified_valid="
            << verified_valid << " verified_invalid=" << verified_invalid
            << " random=" << random_inputs << '\n';
  return 0;
}
