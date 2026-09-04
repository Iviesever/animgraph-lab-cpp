#include "animgraph/asset/codec.hpp"
#include "animgraph/clip/clip.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

using namespace animgraph;

CompiledSkeleton two_joint_skeleton() {
  RawSkeleton raw{{RawJoint{.name = "root", .parent = std::nullopt,
                            .reference_local = Transform::identity(),
                            .inverse_bind = Transform::identity(), .semantic = "root"},
                   RawJoint{.name = "child", .parent = 0,
                            .reference_local = Transform{Vec3{0, 2, 0}, Quat::identity(), Vec3{1, 1, 1}},
                            .inverse_bind = Transform::identity(), .semantic = "hand"}}};
  return compile_skeleton(raw).value();
}

AnimationClip sample_clip_data() {
  AnimationClip clip;
  clip.name = "walk";
  clip.duration = AnimTime{48'000};
  clip.mode = ClipPlaybackMode::loop;
  JointTrack root;
  root.joint = JointId{0};
  root.translations = {{AnimTime{0}, Vec3{0, 0, 0}},
                       {AnimTime{48'000}, Vec3{10, 0, 0}}};
  root.rotations = {{AnimTime{0}, Quat::identity()}};
  root.scales = {{AnimTime{0}, Vec3{1, 1, 1}}};
  clip.tracks.push_back(root);
  clip.events = {{AnimTime{4'800}, "foot_l", 7}, {AnimTime{43'200}, "foot_r", 8}};
  clip.markers = {{AnimTime{0}, "left"}, {AnimTime{24'000}, "right"}};
  return clip;
}

ANIMGRAPH_TEST(animation_time_normalizes_clamp_loop_pingpong_and_zero_duration) {
  AG_CHECK_EQ(normalize_time(AnimTime{-2}, AnimTime{10}, ClipPlaybackMode::clamp)->local.ticks, 0);
  AG_CHECK_EQ(normalize_time(AnimTime{15}, AnimTime{10}, ClipPlaybackMode::clamp)->local.ticks, 10);
  AG_CHECK_EQ(normalize_time(AnimTime{10}, AnimTime{10}, ClipPlaybackMode::loop)->local.ticks, 0);
  AG_CHECK_EQ(normalize_time(AnimTime{-1}, AnimTime{10}, ClipPlaybackMode::loop)->local.ticks, 9);
  AG_CHECK_EQ(normalize_time(AnimTime{12}, AnimTime{10}, ClipPlaybackMode::ping_pong)->local.ticks, 8);
  AG_CHECK(normalize_time(AnimTime{100}, AnimTime{0}, ClipPlaybackMode::loop)->local.ticks == 0);
}

ANIMGRAPH_TEST(clip_sampling_interpolates_and_fills_missing_tracks_from_reference) {
  const auto skeleton = two_joint_skeleton();
  const auto result = sample_clip(skeleton, sample_clip_data(), AnimTime{24'000});
  AG_CHECK(result.has_value());
  AG_CHECK_EQ(result->pose.transforms.size(), 2U);
  AG_CHECK_NEAR(result->pose.transforms[0].translation.x, 5.0F, 1.0e-5F);
  AG_CHECK_NEAR(result->pose.transforms[1].translation.y, 2.0F, 1.0e-5F);
  AG_CHECK_EQ(result->local_time.ticks, 24'000);
}

ANIMGRAPH_TEST(single_key_sampling_and_exact_loop_seam_are_defined) {
  const auto skeleton = two_joint_skeleton();
  auto clip = sample_clip_data();
  clip.tracks[0].translations = {{AnimTime{0}, Vec3{3, 4, 5}}};
  const auto result = sample_clip(skeleton, clip, AnimTime{48'000});
  AG_CHECK(result.has_value());
  AG_CHECK_EQ(result->local_time.ticks, 0);
  AG_CHECK_EQ(result->pose.transforms[0].translation, (Vec3{3, 4, 5}));
}

ANIMGRAPH_TEST(event_query_crosses_loop_seam_without_duplicates) {
  auto clip = sample_clip_data();
  const auto events = query_events(clip, AnimTime{40'000}, AnimTime{52'800});
  AG_CHECK_EQ(events.size(), 2U);
  AG_CHECK_EQ(events[0].name, std::string{"foot_r"});
  AG_CHECK_EQ(events[1].name, std::string{"foot_l"});
  AG_CHECK_EQ(query_events(clip, AnimTime{43'200}, AnimTime{48'000}).size(), 0U);
}

ANIMGRAPH_TEST(sync_markers_require_stable_time_then_name_order_and_unique_pairs) {
  auto clip = sample_clip_data();
  AG_CHECK(validate_clip(clip).has_value());
  std::reverse(clip.markers.begin(), clip.markers.end());
  AG_CHECK(!validate_clip(clip));
  clip = sample_clip_data();
  clip.markers.push_back(clip.markers.front());
  AG_CHECK(!validate_clip(clip));
}

ANIMGRAPH_TEST(skeleton_asset_round_trip_is_byte_stable_and_fail_closed) {
  const auto encoded = encode_skeleton(two_joint_skeleton());
  AG_CHECK(encoded.has_value());
  const auto decoded = decode_skeleton(*encoded);
  AG_CHECK(decoded.has_value());
  AG_CHECK_EQ(decoded->joints.size(), 2U);
  AG_CHECK_EQ(decoded->joints[1].name, std::string{"child"});
  const auto reencoded = encode_skeleton(*decoded).value();
  if (reencoded != *encoded) {
    const auto mismatch = std::mismatch(reencoded.begin(), reencoded.end(), encoded->begin());
    animgraph::test::fail("reencoded == encoded", __FILE__, __LINE__,
                          "first byte offset " + std::to_string(mismatch.first - reencoded.begin()));
  }

  auto bad_magic = *encoded;
  bad_magic[0] = std::byte{0};
  AG_CHECK(!decode_skeleton(bad_magic));
  auto truncated = *encoded;
  truncated.pop_back();
  AG_CHECK(!decode_skeleton(truncated));
  auto corrupt = *encoded;
  corrupt.back() ^= std::byte{1};
  AG_CHECK(!decode_skeleton(corrupt));
}

ANIMGRAPH_TEST(clip_asset_round_trip_is_byte_stable_and_rejects_invalid_data) {
  const auto encoded = encode_clip(sample_clip_data());
  AG_CHECK(encoded.has_value());
  const auto decoded = decode_clip(*encoded);
  if (!decoded) {
    animgraph::test::fail("decoded.has_value()", __FILE__, __LINE__, decoded.error().message);
  }
  AG_CHECK_EQ(decoded->tracks.size(), 1U);
  AG_CHECK_EQ(decoded->events.size(), 2U);
  const auto reencoded = encode_clip(*decoded).value();
  if (reencoded != *encoded) {
    const auto mismatch = std::mismatch(reencoded.begin(), reencoded.end(), encoded->begin());
    animgraph::test::fail("reencoded == encoded", __FILE__, __LINE__,
                          "first byte offset " + std::to_string(mismatch.first - reencoded.begin()));
  }

  auto bad_version = *encoded;
  bad_version[8] = std::byte{99};
  AG_CHECK(!decode_clip(bad_version));
  auto bad_length = *encoded;
  bad_length[16] ^= std::byte{0x40};
  AG_CHECK(!decode_clip(bad_length));
  auto nan_clip = sample_clip_data();
  nan_clip.tracks[0].translations[0].value.x = std::numeric_limits<float>::quiet_NaN();
  AG_CHECK(!encode_clip(nan_clip));
}

ANIMGRAPH_TEST(asset_parsers_survive_10000_bounded_random_inputs) {
  std::uint32_t state = 0xC001CAFEU;
  for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
    state = state * 1'664'525U + 1'013'904'223U;
    std::vector<std::byte> bytes(state % 512U);
    for (auto& byte : bytes) {
      state = state * 1'664'525U + 1'013'904'223U;
      byte = static_cast<std::byte>(state >> 24U);
    }
    static_cast<void>(decode_skeleton(bytes));
    static_cast<void>(decode_clip(bytes));
    static_cast<void>(inspect_asset(bytes));
  }
  AG_CHECK(true);
}

}  // namespace
