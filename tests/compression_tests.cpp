#include "animgraph/asset/codec.hpp"
#include "animgraph/compression/compression.hpp"
#include "test_support.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string>

namespace {

using namespace animgraph;

CompiledSkeleton one_joint_skeleton() {
  return compile_skeleton(RawSkeleton{{RawJoint{.name = "root", .parent = std::nullopt,
      .reference_local = Transform::identity(), .inverse_bind = Transform::identity(),
      .semantic = "root"}}}).value();
}

AnimationClip make_clip(std::string name, const std::vector<std::int64_t>& times,
                        float amplitude, float turns, bool non_linear) {
  AnimationClip clip;
  clip.name = std::move(name);
  clip.duration = AnimTime{times.back()};
  clip.mode = ClipPlaybackMode::clamp;
  JointTrack track;
  track.joint = JointId{0};
  for (const auto tick : times) {
    const float phase = clip.duration.ticks == 0 ? 0.0F
        : static_cast<float>(tick) / static_cast<float>(clip.duration.ticks);
    const float wave = non_linear ? std::sin(phase * 2.0F * std::numbers::pi_v<float>) : phase;
    track.translations.push_back({AnimTime{tick}, Vec3{amplitude * wave, phase, 0}});
    track.rotations.push_back({AnimTime{tick}, *from_axis_angle(Vec3{0, 1, 0},
                                                               turns * phase * 2.0F * std::numbers::pi_v<float>)});
    track.scales.push_back({AnimTime{tick}, Vec3{1.0F + amplitude * 0.1F * wave, 1, 1}});
  }
  clip.tracks.push_back(std::move(track));
  return clip;
}

void verify_error_grid(const AnimationClip& raw, const CompressedClip& compressed,
                       const CompressionSettings& settings) {
  const auto skeleton = one_joint_skeleton();
  std::uint32_t state = 0xA11CEU;
  for (std::size_t sample = 0; sample < 257; ++sample) {
    state = state * 1'664'525U + 1'013'904'223U;
    const auto tick = sample < 129
        ? static_cast<std::int64_t>((raw.duration.ticks * static_cast<std::int64_t>(sample)) / 128)
        : static_cast<std::int64_t>(state % static_cast<std::uint64_t>(raw.duration.ticks + 1));
    const auto a = sample_clip(skeleton, raw, AnimTime{tick}).value().pose.transforms[0];
    const auto b = sample_clip(skeleton, compressed.clip, AnimTime{tick}).value().pose.transforms[0];
    AG_CHECK(length(a.translation - b.translation) <= settings.translation_error + 1.0e-4F);
    AG_CHECK(angular_distance(a.rotation, b.rotation) <= settings.rotation_radians_error + 1.0e-4F);
    AG_CHECK(length(a.scale - b.scale) <= settings.scale_error + 1.0e-4F);
  }
}

ANIMGRAPH_TEST(compression_detects_constant_tracks_and_reports_zero_error) {
  std::vector<std::int64_t> times;
  for (std::int64_t tick = 0; tick <= 48'000; tick += 4'800) times.push_back(tick);
  auto clip = make_clip("static", times, 0.0F, 0.0F, false);
  for (auto& key : clip.tracks[0].translations) key.value = Vec3{2, 3, 4};
  for (auto& key : clip.tracks[0].scales) key.value = Vec3{1, 1, 1};
  CompressionReport report;
  const auto compressed = compress_clip(clip, CompressionSettings{}, report);
  AG_CHECK(compressed.has_value());
  AG_CHECK_EQ(compressed->clip.tracks[0].translations.size(), 1U);
  AG_CHECK_EQ(compressed->clip.tracks[0].rotations.size(), 1U);
  AG_CHECK_EQ(compressed->clip.tracks[0].scales.size(), 1U);
  AG_CHECK_NEAR(report.max_translation_error, 0.0F, 1.0e-6F);
  AG_CHECK(report.compressed_keys < report.raw_keys);
  std::cout << "COMPRESSION static " << report.raw_keys << ' ' << report.compressed_keys
            << ' ' << report.raw_bytes << ' ' << report.compressed_bytes << ' '
            << report.max_translation_error << ' ' << report.max_rotation_error << ' '
            << report.max_scale_error << '\n';
}

ANIMGRAPH_TEST(compression_reduces_linear_keys_and_is_byte_stable) {
  std::vector<std::int64_t> times;
  for (std::int64_t tick = 0; tick <= 48'000; tick += 4'800) times.push_back(tick);
  const auto clip = make_clip("linear", times, 2.0F, 0.25F, false);
  CompressionReport first_report, second_report;
  const CompressionSettings settings{.translation_error = 0.001F,
                                     .rotation_radians_error = 0.001F,
                                     .scale_error = 0.001F};
  const auto first = compress_clip(clip, settings, first_report);
  const auto second = compress_clip(clip, settings, second_report);
  AG_CHECK(first.has_value() && second.has_value());
  const auto first_bytes = encode_clip(first->clip);
  const auto second_bytes = encode_clip(second->clip);
  AG_CHECK(first_bytes.has_value() && second_bytes.has_value());
  AG_CHECK_EQ(*first_bytes, *second_bytes);
  AG_CHECK(first_report.compressed_keys < first_report.raw_keys);
  verify_error_grid(clip, *first, settings);
}

ANIMGRAPH_TEST(compression_error_contract_covers_required_clip_shapes) {
  std::vector<std::int64_t> walk_times;
  for (std::int64_t tick = 0; tick <= 48'000; tick += 1'000) walk_times.push_back(tick);
  std::vector<std::int64_t> long_times;
  for (std::int64_t index = 0; index <= 256; ++index) long_times.push_back(index * 3'750);
  const std::vector<std::int64_t> nonuniform{0, 123, 5'000, 12'345, 31'111, 48'000};
  const std::array clips{
      make_clip("walk", walk_times, 1.0F, 1.0F, true),
      make_clip("rapid_rotation", walk_times, 0.1F, 8.0F, false),
      make_clip("tiny_motion", walk_times, 0.0001F, 0.001F, true),
      make_clip("long", long_times, 2.0F, 4.0F, true),
      make_clip("nonuniform", nonuniform, 0.5F, 0.5F, true)};
  const CompressionSettings settings{.translation_error = 0.02F,
                                     .rotation_radians_error = 0.01F,
                                     .scale_error = 0.01F};
  for (const auto& clip : clips) {
    CompressionReport report;
    const auto compressed = compress_clip(clip, settings, report);
    if (!compressed) {
      animgraph::test::fail("compressed.has_value()", __FILE__, __LINE__,
                            clip.name + ": " + compressed.error().message);
    }
    AG_CHECK(report.compressed_keys <= report.raw_keys);
    AG_CHECK(report.compressed_bytes <= report.raw_bytes);
    AG_CHECK(report.max_translation_error <= settings.translation_error + 1.0e-4F);
    AG_CHECK(report.max_rotation_error <= settings.rotation_radians_error + 1.0e-4F);
    AG_CHECK(report.max_scale_error <= settings.scale_error + 1.0e-4F);
    verify_error_grid(clip, *compressed, settings);
    std::cout << "COMPRESSION " << clip.name << ' ' << report.raw_keys << ' '
              << report.compressed_keys << ' ' << report.raw_bytes << ' '
              << report.compressed_bytes << ' ' << report.max_translation_error << ' '
              << report.max_rotation_error << ' ' << report.max_scale_error << '\n';
  }
}

ANIMGRAPH_TEST(compression_rejects_negative_or_non_finite_thresholds) {
  const auto clip = make_clip("invalid_settings", {0, 48'000}, 1.0F, 1.0F, false);
  CompressionReport report;
  AG_CHECK(!compress_clip(clip, CompressionSettings{.translation_error = -1.0F}, report));
}

}  // namespace
