#include "animgraph/asset/codec.hpp"
#include "animgraph/compression/compression.hpp"
#include "animgraph/runtime/batch.hpp"
#include "animgraph/runtime/blend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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
        .reference_local = Transform{Vec3{0.0001F * static_cast<float>(seed % 997U) +
                                              0.01F * static_cast<float>(index), 1, 0},
                                     Quat::identity(), Vec3{1, 1, 1}},
        .inverse_bind = Transform::identity(), .semantic = std::nullopt});
  }
  return raw;
}

GraphDescription valid_graph(std::uint32_t seed = 0) {
  GraphBuilder builder;
  const auto suffix = std::to_string(seed);
  auto previous = builder.add_node(NodeType::reference_pose, "reference_" + suffix);
  const std::size_t cache_count = 1U + seed % 5U;
  for (std::size_t index = 0; index < cache_count; ++index) {
    const auto cache = builder.add_node(NodeType::pose_cache,
                                        "cache_" + suffix + '_' + std::to_string(index));
    builder.connect(PosePin{previous, 0}, PosePin{cache, 0}).value();
    previous = cache;
  }
  const auto output = builder.add_node(NodeType::output, "output");
  builder.connect(PosePin{previous, 0}, PosePin{output, 0}).value();
  for (std::size_t dead = 0; dead < seed % 3U; ++dead)
    static_cast<void>(builder.add_node(NodeType::reference_pose,
                                       "dead_" + suffix + '_' + std::to_string(dead)));
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
  const auto seed = next(state);
  const auto skeleton = compile_skeleton(valid_skeleton(seed));
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
  const auto skeleton_bytes = encode_skeleton(*skeleton);
  const auto clip_bytes = encode_clip(clip);
  if (!skeleton_bytes || !clip_bytes || encode_skeleton(decode_skeleton(*skeleton_bytes).value()).value() != *skeleton_bytes ||
      encode_clip(decode_clip(*clip_bytes).value()).value() != *clip_bytes) return false;
  const auto description = valid_graph(seed);
  const auto graph_a = compile_graph(description, true);
  const auto graph_b = compile_graph(description, false);
  const auto graph_repeat = compile_graph(description, true);
  if (!graph_a || !graph_b || !graph_repeat || graph_a->identity != graph_repeat->identity ||
      graph_a->pose_slot_count > graph_b->pose_slot_count) return false;
  const auto graph_bytes = encode_graph_plan(*graph_a);
  if (!graph_bytes || decode_graph_plan(*graph_bytes).value() != graph_a->plan_json) return false;
  auto instance_a = make_graph_instance(*graph_a, skeleton->joints.size());
  auto instance_b = make_graph_instance(*graph_b, skeleton->joints.size());
  if (!instance_a || !instance_b) return false;
  const EvaluationContext context{*skeleton, {}, AnimTime{0}, {}, 1, 1};
  const auto evaluated_a = evaluate(context, *graph_a, *instance_a);
  const auto evaluated_b = evaluate(context, *graph_b, *instance_b);
  if (!evaluated_a || !evaluated_b || evaluated_a->pose.transforms != evaluated_b->pose.transforms)
    return false;
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
  const auto category = index % 9U;
  if (category == 0) {
    const auto result = compile_skeleton(RawSkeleton{});
    return !result && result.error().code == ErrorCode::bounds;
  }
  if (category == 1) {
    RawSkeleton raw{{RawJoint{.name = "a", .parent = std::nullopt,
                               .reference_local = Transform::identity(),
                               .inverse_bind = Transform::identity(), .semantic = std::nullopt},
                     RawJoint{.name = "b", .parent = std::nullopt,
                               .reference_local = Transform::identity(),
                               .inverse_bind = Transform::identity(), .semantic = std::nullopt}}};
    const auto result = compile_skeleton(raw);
    return !result && result.error().code == ErrorCode::hierarchy;
  }
  if (category == 2) {
    auto raw = valid_skeleton(1);
    raw.joints[0].parent = 0;
    const auto result = compile_skeleton(raw);
    return !result && result.error().code == ErrorCode::hierarchy;
  }
  if (category == 3) {
    auto raw = valid_skeleton(2);
    raw.joints[0].parent = 1; raw.joints[1].parent = 0;
    const auto result = compile_skeleton(raw);
    return !result && result.error().code == ErrorCode::hierarchy;
  }
  if (category == 4) {
    auto clip = valid_clip(static_cast<std::uint32_t>(index));
    clip.tracks[0].translations[1].time = clip.tracks[0].translations[0].time;
    const auto result = validate_clip(clip);
    return !result && result.error().code == ErrorCode::invalid_argument;
  }
  if (category == 5) {
    auto clip = valid_clip(static_cast<std::uint32_t>(index));
    clip.tracks[0].translations[0].value.x = std::numeric_limits<float>::quiet_NaN();
    const auto result = validate_clip(clip);
    return !result && result.error().code == ErrorCode::non_finite;
  }
  if (category == 6) {
    GraphBuilder builder;
    const auto output = builder.add_node(NodeType::output, "output"); builder.set_output(output);
    const auto result = compile_graph(builder.build());
    return !result && result.error().code == ErrorCode::graph;
  }
  if (category == 7) {
    auto description = valid_graph(static_cast<std::uint32_t>(index));
    description.connections.push_back(description.connections.front());
    const auto result = compile_graph(description);
    return !result && result.error().code == ErrorCode::graph;
  }
  auto description = valid_graph(static_cast<std::uint32_t>(index));
  description.parameters.push_back(GraphParameter{"bad", std::numeric_limits<float>::infinity(), false});
  const auto result = compile_graph(description);
  return !result && result.error().code == ErrorCode::graph;
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
