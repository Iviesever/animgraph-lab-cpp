#include "animgraph/compression/compression.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <utility>

namespace animgraph {
namespace {

template <class Key, class ErrorFunction, class Interpolate>
std::vector<Key> reduce_keys(const std::vector<Key>& source, float tolerance,
                             ErrorFunction error, Interpolate interpolate) {
  if (source.size() <= 1) return source;
  bool constant = true;
  for (std::size_t index = 1; index < source.size(); ++index) {
    if (error(source.front().value, source[index].value) > tolerance) {
      constant = false;
      break;
    }
  }
  if (constant) return {source.front()};

  std::vector<bool> keep(source.size(), false);
  keep.front() = true;
  keep.back() = true;
  std::vector<std::pair<std::size_t, std::size_t>> pending;
  pending.reserve(source.size());
  pending.emplace_back(0, source.size() - 1);
  while (!pending.empty()) {
    const auto [first, last] = pending.back();
    pending.pop_back();
    if (last <= first + 1) continue;
    const auto span = source[last].time.ticks - source[first].time.ticks;
    float maximum = -1.0F;
    std::size_t split = first;
    for (std::size_t index = first + 1; index < last; ++index) {
      const float alpha = span == 0 ? 0.0F
          : static_cast<float>(source[index].time.ticks - source[first].time.ticks) /
            static_cast<float>(span);
      const float candidate = error(source[index].value,
                                    interpolate(source[first].value, source[last].value, alpha));
      if (candidate > maximum) {
        maximum = candidate;
        split = index;
      }
    }
    if (maximum > tolerance) {
      keep[split] = true;
      pending.emplace_back(split, last);
      pending.emplace_back(first, split);
    }
  }
  std::vector<Key> result;
  result.reserve(source.size());
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (keep[index]) result.push_back(source[index]);
  }
  return result;
}

Vec3 interpolate_vec(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
float vec_error(Vec3 a, Vec3 b) { return length(a - b); }
float quat_error(Quat a, Quat b) { return angular_distance(a, b); }

Vec3 sample_reduced(const std::vector<Vec3Key>& keys, AnimTime time) {
  if (keys.empty()) return {};
  if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
  if (time >= keys.back().time) return keys.back().value;
  const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
      [](AnimTime value, const Vec3Key& key) { return value < key.time; });
  const auto lower = upper - 1;
  const float alpha = static_cast<float>(time.ticks - lower->time.ticks) /
                      static_cast<float>(upper->time.ticks - lower->time.ticks);
  return interpolate_vec(lower->value, upper->value, alpha);
}

Quat sample_reduced(const std::vector<QuatKey>& keys, AnimTime time) {
  if (keys.empty()) return Quat::identity();
  if (keys.size() == 1 || time <= keys.front().time) return keys.front().value;
  if (time >= keys.back().time) return keys.back().value;
  const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
      [](AnimTime value, const QuatKey& key) { return value < key.time; });
  const auto lower = upper - 1;
  const float alpha = static_cast<float>(time.ticks - lower->time.ticks) /
                      static_cast<float>(upper->time.ticks - lower->time.ticks);
  return slerp(lower->value, upper->value, alpha);
}

std::optional<float> certified_rotation_path_error(
    const std::vector<QuatKey>& original,
    const std::vector<QuatKey>& reduced) {
  if (original.empty() || reduced.empty() || original == reduced) return 0.0F;
  constexpr std::int64_t max_certification_ticks = 100'000;
  const auto first = original.front().time.ticks;
  const auto last = original.back().time.ticks;
  if (last < first || last - first > max_certification_ticks) return std::nullopt;
  float maximum = 0.0F;
  for (std::int64_t tick = first;; ++tick) {
    maximum = std::max(maximum, angular_distance(
        sample_reduced(original, AnimTime{tick}), sample_reduced(reduced, AnimTime{tick})));
    if (tick == last) break;
  }
  return maximum;
}

bool valid_settings(const CompressionSettings& settings) {
  return std::isfinite(settings.translation_error) && settings.translation_error >= 0.0F &&
         std::isfinite(settings.rotation_radians_error) && settings.rotation_radians_error >= 0.0F &&
         std::isfinite(settings.scale_error) && settings.scale_error >= 0.0F;
}

}  // namespace

Expected<CompressedClip, Error> compress_clip(const AnimationClip& source,
                                              const CompressionSettings& settings,
                                              CompressionReport& report) {
  report = {};
  const auto valid = validate_clip(source);
  if (!valid) return make_unexpected(valid.error());
  if (!valid_settings(settings)) {
    return make_unexpected(Error{ErrorCode::invalid_argument, "compression threshold is invalid"});
  }

  CompressedClip result;
  result.clip = source;
  result.clip.tracks.clear();
  result.clip.tracks.reserve(source.tracks.size());
  std::vector<JointTrack> ordered = source.tracks;
  std::ranges::sort(ordered, {}, [](const JointTrack& track) { return track.joint.value; });

  for (const auto& original : ordered) {
    JointTrack reduced;
    reduced.joint = original.joint;
    reduced.translations = reduce_keys(original.translations, settings.translation_error,
                                       vec_error, interpolate_vec);
    reduced.rotations = reduce_keys(original.rotations, settings.rotation_radians_error,
                                    quat_error, [](Quat a, Quat b, float t) { return slerp(a, b, t); });
    auto rotation_error = certified_rotation_path_error(original.rotations, reduced.rotations);
    if (!rotation_error || *rotation_error > settings.rotation_radians_error) {
      reduced.rotations = original.rotations;
      rotation_error = 0.0F;
    }
    reduced.scales = reduce_keys(original.scales, settings.scale_error, vec_error, interpolate_vec);

    TrackCompressionReport track_report;
    track_report.joint = original.joint;
    track_report.raw_keys = original.translations.size() + original.rotations.size() + original.scales.size();
    track_report.compressed_keys = reduced.translations.size() + reduced.rotations.size() + reduced.scales.size();
    track_report.raw_bytes = (original.translations.size() + original.scales.size()) * 20U +
                             original.rotations.size() * 24U;
    track_report.compressed_bytes = (reduced.translations.size() + reduced.scales.size()) * 20U +
                                    reduced.rotations.size() * 24U;
    for (const auto& key : original.translations) {
      track_report.max_translation_error = std::max(
          track_report.max_translation_error, vec_error(key.value, sample_reduced(reduced.translations, key.time)));
    }
    track_report.max_rotation_error = *rotation_error;
    for (const auto& key : original.scales) {
      track_report.max_scale_error = std::max(
          track_report.max_scale_error, vec_error(key.value, sample_reduced(reduced.scales, key.time)));
    }
    report.raw_keys += track_report.raw_keys;
    report.compressed_keys += track_report.compressed_keys;
    report.raw_bytes += track_report.raw_bytes;
    report.compressed_bytes += track_report.compressed_bytes;
    report.max_translation_error = std::max(report.max_translation_error,
                                            track_report.max_translation_error);
    report.max_rotation_error = std::max(report.max_rotation_error,
                                         track_report.max_rotation_error);
    report.max_scale_error = std::max(report.max_scale_error,
                                      track_report.max_scale_error);
    report.tracks.push_back(track_report);
    result.clip.tracks.push_back(std::move(reduced));
  }
  return result;
}

AnimationClip decompress_clip(const CompressedClip& compressed) { return compressed.clip; }

}  // namespace animgraph
