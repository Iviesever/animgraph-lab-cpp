#include "animgraph/asset/codec.hpp"
#include "animgraph/compression/compression.hpp"
#include "animgraph/runtime/batch.hpp"
#include "animgraph/runtime/blend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <string>
#include <vector>

namespace {

using namespace animgraph;

std::uint32_t next(std::uint32_t& state) {
  state = state * 1'664'525U + 1'013'904'223U;
  return state;
}

RawSkeleton valid_skeleton(std::uint32_t seed) {
  const std::size_t count = 1U + seed % 16U;
  RawSkeleton raw;
  for (std::size_t index = 0; index < count; ++index) {
    raw.joints.push_back(RawJoint{
        .name = "joint_" + std::to_string(index),
        .parent = index == 0 ? std::nullopt
                            : std::optional<std::uint32_t>{static_cast<std::uint32_t>(index - 1)},
        .reference_local = Transform{Vec3{0.01F * static_cast<float>(index), 1, 0},
                                     Quat::identity(), Vec3{1, 1, 1}},
        .inverse_bind = Transform::identity(), .semantic = std::nullopt});
  }
  return raw;
}

GraphDescription valid_graph() {
  GraphBuilder builder;
  const auto reference = builder.add_node(NodeType::reference_pose, "reference");
  const auto cache = builder.add_node(NodeType::pose_cache, "cache");
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{reference, 0}, PosePin{cache, 0}).value();
  builder.connect(PosePin{cache, 0}, PosePin{output, 0}).value();
  builder.set_output(output);
  return builder.build();
}

AnimationClip valid_clip(std::uint32_t seed) {
  AnimationClip clip;
  clip.name = "property";
  clip.duration = AnimTime{1'000};
  clip.mode = ClipPlaybackMode::loop;
  JointTrack track;
  track.joint = JointId{0};
  const float value = static_cast<float>(seed % 100U) / 100.0F;
  track.translations = {{AnimTime{0}, Vec3{0, 0, 0}}, {AnimTime{1'000}, Vec3{value, 0, 0}}};
  track.rotations = {{AnimTime{0}, Quat::identity()},
                     {AnimTime{1'000}, *from_axis_angle(Vec3{0, 1, 0}, value)}};
  track.scales = {{AnimTime{0}, Vec3{1, 1, 1}}, {AnimTime{1'000}, Vec3{1 + value * 0.01F, 1, 1}}};
  clip.tracks.push_back(std::move(track));
  return clip;
}

bool valid_case(std::uint32_t& state) {
  const auto skeleton = compile_skeleton(valid_skeleton(next(state)));
  if (!skeleton) return false;
  LocalPose local;
  for (const auto& joint : skeleton->joints) local.transforms.push_back(joint.reference_local);
  const auto model = local_to_model(*skeleton, local);
  if (!model || model->transforms.size() != skeleton->joints.size()) return false;
  const auto clip = valid_clip(next(state));
  const auto sampled = sample_clip(*skeleton, clip, AnimTime{static_cast<std::int64_t>(next(state) % 2'000U)});
  if (!sampled || sampled->pose.transforms.size() != skeleton->joints.size()) return false;
  for (const auto& transform : sampled->pose.transforms) {
    const auto normalized = normalize(transform.rotation);
    if (!normalized || std::abs(dot(*normalized, *normalized) - 1.0F) > 1.0e-4F) return false;
  }
  CompressionReport report;
  const auto compressed = compress_clip(clip, CompressionSettings{0.01F, 0.01F, 0.01F}, report);
  if (!compressed || report.max_translation_error > 0.0101F ||
      report.max_rotation_error > 0.0101F || report.max_scale_error > 0.0101F) return false;
  const auto graph_a = compile_graph(valid_graph(), true);
  const auto graph_b = compile_graph(valid_graph(), false);
  const auto graph_repeat = compile_graph(valid_graph(), true);
  if (!graph_a || !graph_b || !graph_repeat || graph_a->identity != graph_repeat->identity ||
      graph_a->pose_slot_count > graph_b->pose_slot_count) return false;
  const LocalPose zero = LocalPose{{Transform::identity()}};
  const LocalPose ten = LocalPose{{Transform{Vec3{10, 0, 0}, Quat::identity(), Vec3{1, 1, 1}}}};
  const LocalPose twenty = LocalPose{{Transform{Vec3{20, 0, 0}, Quat::identity(), Vec3{1, 1, 1}}}};
  const float x = static_cast<float>(next(state) % 100U) / 200.0F;
  const float y = static_cast<float>(next(state) % 100U) / 200.0F;
  const auto blend = evaluate_blend_2d(std::array{
      Blend2DSample{Vec2{0, 0}, zero}, Blend2DSample{Vec2{1, 0}, ten},
      Blend2DSample{Vec2{0, 1}, twenty}}, Vec2{x, y});
  if (!blend) return false;
  const float sum = blend->weights[0] + blend->weights[1] + blend->weights[2];
  return std::abs(sum - 1.0F) <= 1.0e-5F &&
         std::ranges::all_of(blend->weights, [](float weight) { return weight >= 0.0F; });
}

bool invalid_case(std::size_t index) {
  if (index % 3U == 0) {
    RawSkeleton raw{{RawJoint{.name = "a", .parent = std::nullopt,
                               .reference_local = Transform::identity(),
                               .inverse_bind = Transform::identity(), .semantic = std::nullopt},
                     RawJoint{.name = "b", .parent = std::nullopt,
                               .reference_local = Transform::identity(),
                               .inverse_bind = Transform::identity(), .semantic = std::nullopt}}};
    return !compile_skeleton(raw);
  }
  if (index % 3U == 1) {
    auto clip = valid_clip(static_cast<std::uint32_t>(index));
    clip.tracks[0].translations[1].time = clip.tracks[0].translations[0].time;
    return !validate_clip(clip) && !encode_clip(clip);
  }
  GraphBuilder builder;
  const auto output = builder.add_node(NodeType::output, "output");
  builder.set_output(output);
  return !compile_graph(builder.build());
}

bool batch_property() {
  const auto skeleton = compile_skeleton(valid_skeleton(1)).value();
  const auto graph = compile_graph(valid_graph()).value();
  constexpr std::size_t count = 1'000;
  std::vector<GraphInstance> serial_instances, parallel_instances;
  std::vector<EvaluationContext> contexts;
  std::vector<EvaluationJob> serial_jobs, parallel_jobs;
  serial_instances.reserve(count); parallel_instances.reserve(count);
  contexts.reserve(count); serial_jobs.reserve(count); parallel_jobs.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    serial_instances.push_back(make_graph_instance(graph, skeleton.joints.size()).value());
    parallel_instances.push_back(make_graph_instance(graph, skeleton.joints.size()).value());
    contexts.push_back(EvaluationContext{skeleton, {}, AnimTime{0}, {}, 1, 1});
  }
  for (std::size_t index = 0; index < count; ++index) {
    serial_jobs.push_back({CharacterId{count - index}, &contexts[index], &graph, &serial_instances[index]});
    parallel_jobs.push_back({CharacterId{count - index}, &contexts[index], &graph, &parallel_instances[index]});
  }
  const auto serial = evaluate_serial(serial_jobs);
  const auto parallel = evaluate_parallel(parallel_jobs, 4);
  if (serial.size() != parallel.size()) return false;
  for (std::size_t index = 0; index < serial.size(); ++index) {
    if (serial[index].error || parallel[index].error || serial[index].character != parallel[index].character ||
        serial[index].result.pose.transforms != parallel[index].result.pose.transforms) return false;
  }
  return true;
}

}  // namespace

int main() {
  std::uint32_t state = 0x51A7E123U;
  std::size_t valid = 0, invalid = 0;
  for (std::size_t index = 0; index < 10'000; ++index) if (valid_case(state)) ++valid;
  for (std::size_t index = 0; index < 10'000; ++index) if (invalid_case(index)) ++invalid;
  const bool batch = batch_property();
  std::cout << "PROPERTY valid=" << valid << " invalid=" << invalid
            << " batch_cases=1000 batch_match=" << (batch ? "true" : "false") << '\n';
  return valid == 10'000 && invalid == 10'000 && batch ? 0 : 1;
}
