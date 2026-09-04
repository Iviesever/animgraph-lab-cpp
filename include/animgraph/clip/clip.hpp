#pragma once

#include "animgraph/core/expected.hpp"
#include "animgraph/core/types.hpp"
#include "animgraph/skeleton/skeleton.hpp"

#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace animgraph {

struct AnimTime {
  std::int64_t ticks{};
  static constexpr std::int64_t ticks_per_second = 48'000;
  auto operator<=>(const AnimTime&) const = default;
};

enum class ClipPlaybackMode : std::uint32_t { clamp = 0, loop = 1, ping_pong = 2 };

struct NormalizedAnimTime {
  AnimTime local;
  std::int64_t cycle{};
  bool reversed{};
};

template <class T>
struct Keyframe {
  AnimTime time;
  T value;
  auto operator<=>(const Keyframe&) const = default;
};

using Vec3Key = Keyframe<Vec3>;
using QuatKey = Keyframe<Quat>;

struct JointTrack {
  JointId joint;
  std::vector<Vec3Key> translations;
  std::vector<QuatKey> rotations;
  std::vector<Vec3Key> scales;
};

struct AnimationEvent {
  AnimTime time;
  std::string name;
  std::int32_t payload{};
};

struct SyncMarker {
  AnimTime time;
  std::string name;
};

struct AnimationClip {
  std::string name;
  AnimTime duration;
  ClipPlaybackMode mode{ClipPlaybackMode::clamp};
  std::vector<JointTrack> tracks;
  std::vector<AnimationEvent> events;
  std::vector<SyncMarker> markers;
};

struct SampleResult {
  LocalPose pose;
  AnimTime local_time;
  std::int64_t cycle{};
  bool reversed{};
};

[[nodiscard]] Expected<NormalizedAnimTime, Error> normalize_time(
    AnimTime time, AnimTime duration, ClipPlaybackMode mode) noexcept;
[[nodiscard]] Expected<void, Error> validate_clip(const AnimationClip& clip);
[[nodiscard]] Expected<SampleResult, Error> sample_clip(const CompiledSkeleton& skeleton,
                                                        const AnimationClip& clip,
                                                        AnimTime time);
[[nodiscard]] std::vector<AnimationEvent> query_events(const AnimationClip& clip,
                                                       AnimTime from, AnimTime to);

}  // namespace animgraph
