#include "animgraph/clip/clip.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace animgraph {
namespace {

std::pair<std::int64_t, std::int64_t> floor_div_mod(std::int64_t value,
                                                    std::int64_t divisor) noexcept {
  std::int64_t quotient = value / divisor;
  std::int64_t remainder = value % divisor;
  if (remainder < 0) {
    remainder += divisor;
    --quotient;
  }
  return {quotient, remainder};
}

template <class Key>
bool valid_keys(const std::vector<Key>& keys, AnimTime duration) {
  if (keys.size() > max_track_keys) return false;
  for (std::size_t index = 0; index < keys.size(); ++index) {
    if (keys[index].time.ticks < 0 || keys[index].time.ticks > duration.ticks) return false;
    if (index > 0 && keys[index - 1].time.ticks >= keys[index].time.ticks) return false;
  }
  return true;
}

Vec3 sample_vec3(const std::vector<Vec3Key>& keys, AnimTime time, Vec3 fallback) {
  if (keys.empty()) return fallback;
  if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
  if (time >= keys.back().time) return keys.back().value;
  const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                      [](AnimTime value, const Vec3Key& key) {
                                        return value < key.time;
                                      });
  const auto lower = upper - 1;
  const float alpha = static_cast<float>(time.ticks - lower->time.ticks) /
                      static_cast<float>(upper->time.ticks - lower->time.ticks);
  return lower->value + (upper->value - lower->value) * alpha;
}

Quat sample_quat(const std::vector<QuatKey>& keys, AnimTime time, Quat fallback) {
  if (keys.empty()) return fallback;
  if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
  if (time >= keys.back().time) return keys.back().value;
  const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                      [](AnimTime value, const QuatKey& key) {
                                        return value < key.time;
                                      });
  const auto lower = upper - 1;
  const float alpha = static_cast<float>(time.ticks - lower->time.ticks) /
                      static_cast<float>(upper->time.ticks - lower->time.ticks);
  return slerp(lower->value, upper->value, alpha);
}

bool event_less(const AnimationEvent& a, const AnimationEvent& b) {
  return std::tie(a.time.ticks, a.name, a.payload) < std::tie(b.time.ticks, b.name, b.payload);
}
bool marker_less(const SyncMarker& a, const SyncMarker& b) {
  return std::tie(a.time.ticks, a.name) < std::tie(b.time.ticks, b.name);
}

}  // namespace

Expected<NormalizedAnimTime, Error> normalize_time(AnimTime time, AnimTime duration,
                                                   ClipPlaybackMode mode) noexcept {
  if (duration.ticks < 0) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "clip duration is negative"});
  }
  if (duration.ticks == 0) return NormalizedAnimTime{AnimTime{0}, 0, false};
  switch (mode) {
    case ClipPlaybackMode::clamp:
      return NormalizedAnimTime{AnimTime{std::clamp(time.ticks, std::int64_t{0}, duration.ticks)},
                                0, false};
    case ClipPlaybackMode::loop: {
      const auto [cycle, local] = floor_div_mod(time.ticks, duration.ticks);
      return NormalizedAnimTime{AnimTime{local}, cycle, false};
    }
    case ClipPlaybackMode::ping_pong: {
      if (duration.ticks > std::numeric_limits<std::int64_t>::max() / 2) {
        return make_unexpected(Error{ErrorCode::bounds, "ping-pong period overflows"});
      }
      const auto [cycle, local] = floor_div_mod(time.ticks, duration.ticks * 2);
      const bool reversed = local > duration.ticks;
      return NormalizedAnimTime{AnimTime{reversed ? duration.ticks * 2 - local : local},
                                cycle, reversed};
    }
  }
  return make_unexpected(Error{ErrorCode::unsupported, "unknown playback mode"});
}

Expected<void, Error> validate_clip(const AnimationClip& clip) {
  if (clip.name.empty() || clip.duration.ticks < 0) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "clip name or duration is invalid"});
  }
  if (clip.events.size() > max_events || clip.markers.size() > max_events) {
    return make_unexpected(Error{ErrorCode::bounds, "event or marker count exceeds limit"});
  }
  std::vector<std::uint32_t> joints;
  joints.reserve(clip.tracks.size());
  for (const auto& track : clip.tracks) {
    if (track.joint.value >= max_joints ||
        !valid_keys(track.translations, clip.duration) ||
        !valid_keys(track.rotations, clip.duration) ||
        !valid_keys(track.scales, clip.duration)) {
      return make_unexpected(Error{ErrorCode::invalid_argument, "joint track is invalid"});
    }
    for (const auto& key : track.translations) if (!finite(key.value))
      return make_unexpected(Error{ErrorCode::non_finite, "translation key is not finite"});
    for (const auto& key : track.scales) if (!finite(key.value))
      return make_unexpected(Error{ErrorCode::non_finite, "scale key is not finite"});
    for (const auto& key : track.rotations) if (!normalize(key.value))
      return make_unexpected(Error{ErrorCode::non_finite, "rotation key is invalid"});
    joints.push_back(track.joint.value);
  }
  std::sort(joints.begin(), joints.end());
  if (std::adjacent_find(joints.begin(), joints.end()) != joints.end()) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "duplicate joint track"});
  }
  for (std::size_t index = 0; index < clip.events.size(); ++index) {
    const auto& event = clip.events[index];
    if (event.name.empty() || event.time.ticks < 0 || event.time.ticks > clip.duration.ticks ||
        (index > 0 && !event_less(clip.events[index - 1], event))) {
      return make_unexpected(Error{ErrorCode::invalid_argument, "event track is not strictly sorted"});
    }
  }
  for (std::size_t index = 0; index < clip.markers.size(); ++index) {
    const auto& marker = clip.markers[index];
    if (marker.name.empty() || marker.time.ticks < 0 || marker.time.ticks > clip.duration.ticks ||
        (index > 0 && !marker_less(clip.markers[index - 1], marker))) {
      return make_unexpected(Error{ErrorCode::invalid_argument, "marker track is not strictly sorted"});
    }
  }
  return {};
}

Expected<SampleResult, Error> sample_clip(const CompiledSkeleton& skeleton,
                                         const AnimationClip& clip, AnimTime time) {
  const auto valid = validate_clip(clip);
  if (!valid) return make_unexpected(valid.error());
  const auto normalized = normalize_time(time, clip.duration, clip.mode);
  if (!normalized) return make_unexpected(normalized.error());

  SampleResult result;
  result.local_time = normalized->local;
  result.cycle = normalized->cycle;
  result.reversed = normalized->reversed;
  result.pose.transforms.reserve(skeleton.joints.size());
  for (const auto& joint : skeleton.joints) result.pose.transforms.push_back(joint.reference_local);

  for (const auto& track : clip.tracks) {
    if (track.joint.value >= result.pose.transforms.size()) {
      return make_unexpected(Error{ErrorCode::bounds, "clip track joint is outside skeleton"});
    }
    auto& transform = result.pose.transforms[track.joint.value];
    transform.translation = sample_vec3(track.translations, normalized->local, transform.translation);
    transform.rotation = sample_quat(track.rotations, normalized->local, transform.rotation);
    transform.scale = sample_vec3(track.scales, normalized->local, transform.scale);
  }
  return result;
}

Expected<std::vector<AnimationEventOccurrence>, Error> query_event_occurrences(
    const AnimationClip& clip, AnimTime from, AnimTime to) {
  std::vector<AnimationEventOccurrence> result;
  const auto valid = validate_clip(clip);
  if (!valid) return make_unexpected(valid.error());
  if (to.ticks <= from.ticks || clip.duration.ticks == 0) return result;
  if (clip.mode != ClipPlaybackMode::loop) {
    const auto start = normalize_time(from, clip.duration, ClipPlaybackMode::clamp).value().local;
    const auto end = normalize_time(to, clip.duration, ClipPlaybackMode::clamp).value().local;
    for (const auto& event : clip.events) {
      if (event.time > start && event.time <= end)
        result.push_back(AnimationEventOccurrence{event, event.time, 0});
    }
    return result;
  }

  const auto [first_cycle, unused_first] = floor_div_mod(from.ticks, clip.duration.ticks);
  const auto [last_cycle, unused_last] = floor_div_mod(to.ticks, clip.duration.ticks);
  static_cast<void>(unused_first);
  static_cast<void>(unused_last);
  const std::int64_t bounded_last = std::min(last_cycle, first_cycle + 4096);
  for (std::int64_t cycle = first_cycle; cycle <= bounded_last; ++cycle) {
    for (const auto& event : clip.events) {
      const long double wide = static_cast<long double>(cycle) *
          static_cast<long double>(clip.duration.ticks) + static_cast<long double>(event.time.ticks);
      if (wide < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
          wide > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return make_unexpected(Error{ErrorCode::bounds, "event occurrence time overflows"});
      }
      const auto absolute = static_cast<std::int64_t>(wide);
      if (absolute > from.ticks && absolute <= to.ticks)
        result.push_back(AnimationEventOccurrence{event, AnimTime{absolute}, cycle});
      if (result.size() >= max_events) return result;
    }
  }
  return result;
}

std::vector<AnimationEvent> query_events(const AnimationClip& clip, AnimTime from,
                                        AnimTime to) {
  std::vector<AnimationEvent> result;
  const auto occurrences = query_event_occurrences(clip, from, to);
  if (!occurrences) return result;
  result.reserve(occurrences->size());
  for (const auto& occurrence : *occurrences) result.push_back(occurrence.event);
  return result;
}

}  // namespace animgraph
