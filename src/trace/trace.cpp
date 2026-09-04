#include "animgraph/trace/trace.hpp"

#include "animgraph/core/version.hpp"
#include "animgraph/runtime/batch.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <string_view>
#include <tuple>

namespace animgraph {
namespace {

std::string escape_json(std::string_view value) {
  std::string result;
  for (const char character : value) {
    switch (character) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += static_cast<unsigned char>(character) < 0x20U ? '?' : character;
    }
  }
  return result;
}

std::string base64_encode(std::string_view value) {
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((value.size() + 2) / 3) * 4);
  for (std::size_t index = 0; index < value.size(); index += 3) {
    const std::uint32_t first = static_cast<unsigned char>(value[index]);
    const std::uint32_t second = index + 1 < value.size()
        ? static_cast<unsigned char>(value[index + 1]) : 0;
    const std::uint32_t third = index + 2 < value.size()
        ? static_cast<unsigned char>(value[index + 2]) : 0;
    const std::uint32_t bits = (first << 16U) | (second << 8U) | third;
    result.push_back(alphabet[(bits >> 18U) & 63U]);
    result.push_back(alphabet[(bits >> 12U) & 63U]);
    result.push_back(index + 1 < value.size() ? alphabet[(bits >> 6U) & 63U] : '=');
    result.push_back(index + 2 < value.size() ? alphabet[bits & 63U] : '=');
  }
  return result;
}

void json_vec3(std::ostream& output, Vec3 value) {
  output << '[' << value.x << ',' << value.y << ',' << value.z << ']';
}
void json_quat(std::ostream& output, Quat value) {
  output << '[' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << ']';
}
void json_transform(std::ostream& output, const Transform& value) {
  output << "{\"t\":"; json_vec3(output, value.translation);
  output << ",\"r\":"; json_quat(output, value.rotation);
  output << ",\"s\":"; json_vec3(output, value.scale); output << '}';
}
void json_pose(std::ostream& output, const std::vector<Transform>& transforms) {
  output << '[';
  for (std::size_t index = 0; index < transforms.size(); ++index) {
    if (index) output << ',';
    json_transform(output, transforms[index]);
  }
  output << ']';
}

std::string compiler_name() {
#if defined(_MSC_VER)
  return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
  return "Clang " + std::to_string(__clang_major__);
#elif defined(__GNUC__)
  return "GCC " + std::to_string(__GNUC__);
#else
  return "Unknown compiler";
#endif
}
std::string os_name() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__linux__)
  return "Linux";
#else
  return "Unknown OS";
#endif
}
std::string cpu_name() {
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, "PROCESSOR_IDENTIFIER") == 0 && value) {
    std::string result{value};
    std::free(value);
    return result;
  }
  std::free(value);
#else
  if (const char* value = std::getenv("PROCESSOR_IDENTIFIER")) return value;
  if (const char* value = std::getenv("HOSTTYPE")) return value;
#endif
  return "not reported by environment";
}

Expected<BenchmarkRow, Error> measure_graph(DemoBundle& demo, std::string scenario,
                                            std::size_t count, std::size_t workers) {
  const std::array parameters{0.5F, -1.5F, 0.0F, 1.0F,
                              -1.4F, 2.0F, 0.2F, 0.75F};
  std::vector<GraphInstance> instances;
  std::vector<EvaluationContext> contexts;
  std::vector<EvaluationJob> jobs;
  instances.reserve(count); contexts.reserve(count); jobs.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    auto instance = make_graph_instance(demo.graph, demo.skeleton.joints.size());
    if (!instance) return make_unexpected(instance.error());
    instances.push_back(std::move(*instance));
    contexts.push_back(EvaluationContext{demo.skeleton, demo.clips, AnimTime{800},
                                         parameters, 1, 1});
  }
  for (std::size_t index = 0; index < count; ++index)
    jobs.push_back(EvaluationJob{CharacterId{index}, &contexts[index], &demo.graph, &instances[index]});
  const auto begin = std::chrono::steady_clock::now();
  const auto results = workers > 1 ? evaluate_parallel(jobs, workers) : evaluate_serial(jobs);
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - begin).count();
  std::size_t hits = 0, misses = 0;
  for (const auto& result : results) {
    if (result.error) return make_unexpected(*result.error);
    hits += result.result.pose_cache_hits; misses += result.result.pose_cache_misses;
  }
  const auto total = static_cast<std::uint64_t>(std::max<std::int64_t>(1, elapsed));
  return BenchmarkRow{std::move(scenario), count, demo.skeleton.joints.size(),
      demo.graph.instructions.size(), count * demo.graph.instructions.size(), total,
      static_cast<double>(total) / static_cast<double>(count),
      static_cast<double>(total) / static_cast<double>(count * demo.skeleton.joints.size()),
      hits + misses == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(hits + misses),
      demo.graph.pose_slot_count, demo.compression.raw_bytes, demo.compression.compressed_bytes,
      demo.compression.max_translation_error, demo.compression.max_rotation_error,
      demo.compression.max_scale_error, workers, 0, 0};
}

Expected<BenchmarkRow, Error> measure_clip_only(DemoBundle& demo) {
  const auto begin = std::chrono::steady_clock::now();
  for (std::size_t sample = 0; sample < 1000; ++sample) {
    const auto pose = sample_clip(demo.skeleton, demo.clips[1],
        AnimTime{static_cast<std::int64_t>((sample * 791U) % 48'001U)});
    if (!pose) return make_unexpected(pose.error());
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - begin).count();
  const auto total = static_cast<std::uint64_t>(std::max<std::int64_t>(1, elapsed));
  return BenchmarkRow{"clip_only", 1000, demo.skeleton.joints.size(), 0, 1000, total,
      static_cast<double>(total) / 1000.0, static_cast<double>(total) /
          static_cast<double>(1000 * demo.skeleton.joints.size()),
      0.0, 0, demo.compression.raw_bytes, demo.compression.compressed_bytes,
      demo.compression.max_translation_error, demo.compression.max_rotation_error,
      demo.compression.max_scale_error, 1, 0, 0};
}

Expected<BenchmarkRow, Error> measure_compressed_vs_raw(DemoBundle& demo) {
  CompressionReport report;
  const auto compressed = compress_clip(demo.clips[1], CompressionSettings{0.02F, 0.01F, 0.01F}, report);
  if (!compressed) return make_unexpected(compressed.error());
  const auto raw_begin = std::chrono::steady_clock::now();
  for (std::size_t sample = 0; sample < 100; ++sample) {
    const AnimTime time{static_cast<std::int64_t>((sample * 479U) % 48'001U)};
    const auto raw = sample_clip(demo.skeleton, demo.clips[1], time);
    if (!raw) return make_unexpected(raw.error());
  }
  const auto raw_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - raw_begin).count();
  const auto compressed_begin = std::chrono::steady_clock::now();
  for (std::size_t sample = 0; sample < 100; ++sample) {
    const AnimTime time{static_cast<std::int64_t>((sample * 479U) % 48'001U)};
    const auto reduced = sample_clip(demo.skeleton, compressed->clip, time);
    if (!reduced) return make_unexpected(reduced.error());
  }
  const auto compressed_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - compressed_begin).count();
  const auto raw_ns = static_cast<std::uint64_t>(std::max<std::int64_t>(1, raw_elapsed));
  const auto compressed_ns = static_cast<std::uint64_t>(std::max<std::int64_t>(1, compressed_elapsed));
  const auto total = raw_ns + compressed_ns;
  return BenchmarkRow{"compressed_vs_raw", 100, demo.skeleton.joints.size(), 0, 200,
      total, static_cast<double>(total) / 100.0,
      static_cast<double>(total) / static_cast<double>(200 * demo.skeleton.joints.size()),
      0.0, 0, report.raw_bytes, report.compressed_bytes,
      report.max_translation_error, report.max_rotation_error, report.max_scale_error,
      1, raw_ns, compressed_ns};
}

Expected<BenchmarkRow, Error> measure_state_transition(DemoBundle& demo) {
  StateMachineDefinition definition;
  definition.states = {{StateId{0}, "Idle", AnimTime{48'000}},
                       {StateId{1}, "Run", AnimTime{48'000}}};
  definition.entry = StateId{0};
  definition.transitions = {TransitionDefinition{StateId{0}, StateId{1},
      ParameterCondition{0, CompareOp::greater, 0.5F}, 1, AnimTime{12'000},
      std::nullopt, std::nullopt, false}};
  std::vector<StateMachineInstance> instances;
  instances.reserve(100);
  for (std::size_t index = 0; index < 100; ++index)
    instances.push_back(make_state_machine_instance(definition).value());
  const std::array parameters{1.0F};
  const auto begin = std::chrono::steady_clock::now();
  for (auto& instance : instances) {
    const auto update = update_state_machine(definition, parameters, AnimTime{800}, instance);
    if (!update) return make_unexpected(update.error());
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - begin).count();
  const auto total = static_cast<std::uint64_t>(std::max<std::int64_t>(1, elapsed));
  return BenchmarkRow{"state_transition",100,demo.skeleton.joints.size(),1,100,total,
      static_cast<double>(total)/100.0,static_cast<double>(total)/(100.0*demo.skeleton.joints.size()),
      0.0,0,demo.compression.raw_bytes,demo.compression.compressed_bytes,
      demo.compression.max_translation_error,demo.compression.max_rotation_error,
      demo.compression.max_scale_error,1,0,0};
}

Expected<BenchmarkRow, Error> measure_ik(DemoBundle& demo) {
  std::vector<LocalPose> poses(100);
  for (auto& pose : poses)
    for (const auto& joint : demo.skeleton.joints) pose.transforms.push_back(joint.reference_local);
  const auto begin = std::chrono::steady_clock::now();
  for (auto& pose : poses) {
    const auto solved = solve_two_bone_ik(demo.skeleton, pose,
        TwoBoneIkRequest{JointId{0},JointId{1},JointId{2},Vec3{0.5F,-1.5F,0},
                         Vec3{0,0,1},1.0F,std::nullopt});
    if (!solved) return make_unexpected(solved.error());
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - begin).count();
  const auto total = static_cast<std::uint64_t>(std::max<std::int64_t>(1, elapsed));
  return BenchmarkRow{"ik_enabled",100,demo.skeleton.joints.size(),1,100,total,
      static_cast<double>(total)/100.0,static_cast<double>(total)/(100.0*demo.skeleton.joints.size()),
      0.0,0,demo.compression.raw_bytes,demo.compression.compressed_bytes,
      demo.compression.max_translation_error,demo.compression.max_rotation_error,
      demo.compression.max_scale_error,1,0,0};
}

}  // namespace

Expected<TraceDocument, Error> generate_demo_trace(
    DemoBundle& demo, std::size_t frame_count, AnimTime delta, std::string git_sha) {
  if (frame_count == 0 || frame_count > 10'000 || delta.ticks < 0)
    return make_unexpected(Error{ErrorCode::bounds, "trace frame request is invalid"});
  auto instance = make_graph_instance(demo.graph, demo.skeleton.joints.size());
  if (!instance) return make_unexpected(instance.error());
  TraceDocument trace;
  trace.git_sha = std::move(git_sha);
  trace.skeleton = demo.skeleton;
  trace.graph = demo.graph;
  trace.compression = demo.compression;
  trace.frames.reserve(frame_count);
  const std::array parameters{0.5F, -1.5F, 0.0F, 1.0F,
                              -1.4F, 2.0F, 0.2F, 0.75F};
  for (std::size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    const EvaluationContext context{demo.skeleton, demo.clips, delta, parameters,
                                    frame_index + 1, 1};
    const auto begin = std::chrono::steady_clock::now();
    const auto evaluated = evaluate(context, demo.graph, *instance);
    const double microseconds = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - begin).count();
    if (!evaluated) return make_unexpected(evaluated.error());
    const auto model = local_to_model(demo.skeleton, evaluated->pose);
    if (!model) return make_unexpected(model.error());
    TraceFrame frame;
    frame.index = static_cast<std::uint32_t>(frame_index);
    frame.time = AnimTime{static_cast<std::int64_t>((frame_index + 1) * delta.ticks)};
    frame.local_pose = evaluated->pose;
    frame.model_pose = *model;
    frame.current_node = evaluated->current_node;
    frame.executed_nodes = evaluated->executed_nodes;
    frame.state = evaluated->state.empty() ? "None" : evaluated->state;
    frame.transition = evaluated->transition_progress;
    frame.blends = evaluated->blends;
    if (!frame.blends.empty()) frame.blend_weights = frame.blends.back().weights;
    frame.pose_cache_hits = evaluated->pose_cache_hits;
    frame.pose_cache_misses = evaluated->pose_cache_misses;
    frame.root_motion = evaluated->root_motion;
    frame.root_accumulated = evaluated->root_accumulated;
    frame.ik_applied = evaluated->ik_applied;
    frame.ik_target = evaluated->ik_target;
    frame.ik_pole = evaluated->ik_pole;
    frame.ik_error = evaluated->ik_error;
    frame.ik_nodes = evaluated->ik_nodes;
    frame.evaluation_microseconds = microseconds;
    frame.success = true;
    for (const auto& event : evaluated->events) frame.events.push_back(event.name);
    frame.event_occurrences = evaluated->event_occurrences;
    frame.sync_markers = evaluated->sync_markers;
    frame.sync_marker_occurrences = evaluated->sync_marker_occurrences;
    for (std::size_t index = 0; index < demo.graph.instructions.size(); ++index) {
      const auto& instruction = demo.graph.instructions[index];
      if (instruction.clip_index)
        frame.clip_times.push_back({instruction.name, demo.clips[*instruction.clip_index].name,
                                    instance->clip_times[index]});
    }
    trace.frames.push_back(std::move(frame));
  }
  return trace;
}

std::string trace_to_json(const TraceDocument& trace) {
  std::ostringstream output;
  output << std::setprecision(9);
  output << "{\"trace_version\":" << trace.version << ",\"git_sha\":\""
         << escape_json(trace.git_sha) << "\",\"asset_version\":" << trace.asset_version
         << ",\"graph_plan_version\":" << trace.graph_plan_version << ",\"success\":"
         << (trace.success ? "true" : "false");
  if (!trace.success) {
    output << ",\"error\":{\"code\":" << static_cast<int>(trace.error_code)
           << ",\"message\":\"" << escape_json(trace.error_message) << "\"}";
  }
  output << ",\"skeleton\":{" << "\"joints\":[";
  for (std::size_t index = 0; index < trace.skeleton.joints.size(); ++index) {
    if (index) output << ',';
    const auto& joint = trace.skeleton.joints[index];
    output << "{\"id\":" << joint.id.value << ",\"name\":\"" << escape_json(joint.name)
           << "\",\"parent\":";
    if (joint.parent) output << joint.parent->value; else output << "null";
    output << '}';
  }
  output << "]},\"graph_plan\":";
  if (trace.graph.plan_json.empty()) output << "null";
  else output << trace.graph.plan_json;
  output << ",\"graph_identity\":" << trace.graph.identity;
  output << ",\"compression\":{\"raw_keys\":" << trace.compression.raw_keys
         << ",\"compressed_keys\":" << trace.compression.compressed_keys
         << ",\"raw_bytes\":" << trace.compression.raw_bytes
         << ",\"compressed_bytes\":" << trace.compression.compressed_bytes
         << ",\"max_translation_error\":" << trace.compression.max_translation_error
         << ",\"max_rotation_error\":" << trace.compression.max_rotation_error
         << ",\"max_scale_error\":" << trace.compression.max_scale_error << '}';
  output << ",\"allocation_counts\":null,\"frames\":[";
  for (std::size_t frame_index = 0; frame_index < trace.frames.size(); ++frame_index) {
    if (frame_index) output << ',';
    const auto& frame = trace.frames[frame_index];
    output << "{\"index\":" << frame.index << ",\"ticks\":" << frame.time.ticks
           << ",\"success\":" << (frame.success ? "true" : "false")
           << ",\"current_node\":\"" << escape_json(frame.current_node)
           << "\",\"state\":\"" << escape_json(frame.state) << "\",\"transition\":"
           << frame.transition << ",\"local_pose\":";
    json_pose(output, frame.local_pose.transforms);
    output << ",\"model_pose\":"; json_pose(output, frame.model_pose.transforms);
    output << ",\"executed_nodes\":[";
    for (std::size_t node = 0; node < frame.executed_nodes.size(); ++node) {
      if (node) output << ',';
      output << '"' << escape_json(frame.executed_nodes[node]) << '"';
    }
    output << ']';
    output << ",\"clip_times\":[";
    for (std::size_t index = 0; index < frame.clip_times.size(); ++index) {
      if (index) output << ',';
      output << "{\"node\":\"" << escape_json(frame.clip_times[index].node)
             << "\",\"clip\":\"" << escape_json(frame.clip_times[index].clip)
             << "\",\"ticks\":" << frame.clip_times[index].time.ticks << '}';
    }
    output << "],\"blend_weights\":[";
    for (std::size_t index = 0; index < frame.blend_weights.size(); ++index) {
      if (index) output << ',';
      output << frame.blend_weights[index];
    }
    output << "],\"blend_nodes\":[";
    for (std::size_t blend = 0; blend < frame.blends.size(); ++blend) {
      if (blend) output << ',';
      output << "{\"node\":" << frame.blends[blend].node.value << ",\"name\":\""
             << escape_json(frame.blends[blend].name) << "\",\"weights\":[";
      for (std::size_t weight = 0; weight < frame.blends[blend].weights.size(); ++weight) {
        if (weight) output << ',';
        output << frame.blends[blend].weights[weight];
      }
      output << "]}";
    }
    output << "],\"pose_cache\":{\"hits\":" << frame.pose_cache_hits
           << ",\"misses\":" << frame.pose_cache_misses << "},\"events\":[";
    for (std::size_t index = 0; index < frame.events.size(); ++index) {
      if (index) output << ',';
      output << '"' << escape_json(frame.events[index]) << '"';
    }
    output << "],\"sync_markers\":[";
    for (std::size_t index = 0; index < frame.sync_markers.size(); ++index) {
      if (index) output << ',';
      output << '"' << escape_json(frame.sync_markers[index]) << '"';
    }
    output << "],\"sync_marker_occurrences\":[";
    for (std::size_t marker = 0; marker < frame.sync_marker_occurrences.size(); ++marker) {
      if (marker) output << ',';
      const auto& occurrence = frame.sync_marker_occurrences[marker];
      output << "{\"name\":\"" << escape_json(occurrence.marker.name)
             << "\",\"local_tick\":" << occurrence.marker.time.ticks
             << ",\"absolute_tick\":" << occurrence.absolute_time.ticks
             << ",\"cycle\":" << occurrence.cycle << ",\"source_node\":"
             << occurrence.source_node.value << ",\"clip\":" << occurrence.clip_index << '}';
    }
    output << "],\"event_occurrences\":[";
    for (std::size_t event = 0; event < frame.event_occurrences.size(); ++event) {
      if (event) output << ',';
      const auto& occurrence = frame.event_occurrences[event];
      output << "{\"name\":\"" << escape_json(occurrence.event.name)
             << "\",\"absolute_tick\":" << occurrence.absolute_time.ticks
             << ",\"cycle\":" << occurrence.cycle << ",\"source_node\":"
             << occurrence.source_node.value << ",\"clip\":" << occurrence.clip_index << '}';
    }
    output << "],\"root_motion\":"; json_transform(output, frame.root_motion);
    output << ",\"root_accumulated\":"; json_transform(output, frame.root_accumulated);
    output << ",\"ik\":{\"applied\":" << (frame.ik_applied ? "true" : "false")
           << ",\"target\":"; json_vec3(output, frame.ik_target);
    output << ",\"pole\":"; json_vec3(output, frame.ik_pole);
    output << ",\"error\":" << frame.ik_error << "},\"ik_nodes\":[";
    for (std::size_t ik = 0; ik < frame.ik_nodes.size(); ++ik) {
      if (ik) output << ',';
      const auto& observation = frame.ik_nodes[ik];
      output << "{\"node\":" << observation.node.value << ",\"name\":\""
             << escape_json(observation.name) << "\",\"chain\":["
             << observation.root.value << ',' << observation.mid.value << ','
             << observation.end.value << "],\"target\":";
      json_vec3(output, observation.target); output << ",\"pole\":";
      json_vec3(output, observation.pole); output << ",\"error\":"
             << observation.error << ",\"status\":" << static_cast<int>(observation.status) << '}';
    }
    output << "],\"evaluation_us\":"
           << frame.evaluation_microseconds << '}';
  }
  output << "]}";
  return output.str();
}

namespace {

std::string secure_viewer_html(std::string_view trace_json) {
  std::string html = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>AnimGraphLab Trace</title>
<style>:root{color-scheme:dark;--bg:#0b1020;--panel:#151d32;--line:#6ee7ff;--hot:#ffcf66;--text:#e8eefc}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 20% 0,#1b3154,var(--bg) 45%);color:var(--text);font:14px system-ui,sans-serif}header{padding:18px 22px;border-bottom:1px solid #2d3853}h1{margin:0;font-size:20px}main{display:grid;grid-template-columns:minmax(320px,2fr) minmax(280px,1fr);gap:12px;padding:12px}.panel{background:#151d32;border:1px solid #2b3856;border-radius:12px;padding:12px;min-width:0}canvas{width:100%;height:320px;background:#080d18;border-radius:8px}#rootCanvas,#compressionCanvas{height:130px}.controls{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:10px}button,select,input{accent-color:var(--line);background:#202b47;color:var(--text);border:1px solid #415173;border-radius:7px;padding:7px}#timeline{flex:1;min-width:140px}.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 10px}.nodes,.chips{display:flex;flex-wrap:wrap;gap:5px}.node,.chip{padding:4px 7px;border-radius:6px;background:#27334f}.node.active{background:var(--hot);color:#1b1b1b}@media(max-width:760px){main{grid-template-columns:1fr}canvas{height:270px}header{padding:14px}}</style></head><body>
<header><h1>AnimGraphLab · Runtime Trace Debugger</h1></header><main><section class="panel"><div class="controls"><button id="playPause">Play</button><button id="step">Step</button><input id="timeline" type="range" min="0" value="0"><select id="spaceMode"><option value="model_pose">Model</option><option value="local_pose">Local hierarchy</option></select><span id="frameLabel"></span></div><canvas id="skeletonCanvas"></canvas><h3>Root Motion</h3><canvas id="rootCanvas"></canvas><h3>Compression: raw vs reduced</h3><canvas id="compressionCanvas"></canvas></section><aside class="panel"><div id="stats" class="kv"></div><h3>Events</h3><div id="events" class="chips"></div><h3>Crossed Sync Markers</h3><div id="markers" class="chips"></div><h3>Graph Nodes</h3><div id="graphNodes" class="nodes"></div><h3>Compression Errors</h3><div id="compression" class="kv"></div></aside></main>
<script id="trace-data" type="text/plain">)HTML";
  html += base64_encode(trace_json);
  html += R"HTML(</script><script>'use strict';
const bytes=Uint8Array.from(atob(document.getElementById('trace-data').textContent.trim()),c=>c.charCodeAt(0));
const trace=JSON.parse(new TextDecoder().decode(bytes)),frames=trace.frames||[],slider=document.getElementById('timeline');
const $=id=>document.getElementById(id),esc=v=>String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
let index=0,timer=null;slider.max=Math.max(0,frames.length-1);
function fit(c){const d=devicePixelRatio||1,r=c.getBoundingClientRect();if(c.width!==r.width*d||c.height!==r.height*d){c.width=r.width*d;c.height=r.height*d}return d}
function qmul(a,b){return[a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]]}
function qrot(q,v){const u=q.slice(0,3),s=q[3],dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2],cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];const uv=cross(u,v),uuv=cross(u,uv);return[v[0]+2*(s*uv[0]+uuv[0]),v[1]+2*(s*uv[1]+uuv[1]),v[2]+2*(s*uv[2]+uuv[2])]}
function localToModel(local){const out=[];trace.skeleton.joints.forEach((j,i)=>{const t=local[i];if(j.parent===null){out.push(t);return}const p=out[j.parent],scaled=[t.t[0]*p.s[0],t.t[1]*p.s[1],t.t[2]*p.s[2]],rot=qrot(p.r,scaled);out.push({t:[p.t[0]+rot[0],p.t[1]+rot[1],p.t[2]+rot[2]],r:qmul(p.r,t.r),s:[p.s[0]*t.s[0],p.s[1]*t.s[1],p.s[2]*t.s[2]]})});return out}
function screen(v,w,h){return{x:w/2+v[0]*55,y:h*.72-v[1]*55}}
function drawSkeleton(){const c=$('skeletonCanvas'),d=fit(c),x=c.getContext('2d'),w=c.width/d,h=c.height/d;x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,w,h);const f=frames[index],pose=$('spaceMode').value==='local_pose'?localToModel(f.local_pose):f.model_pose,pts=pose.map(t=>screen(t.t,w,h));x.lineWidth=3;x.strokeStyle='#6ee7ff';trace.skeleton.joints.forEach((j,i)=>{if(j.parent!==null){x.beginPath();x.moveTo(pts[j.parent].x,pts[j.parent].y);x.lineTo(pts[i].x,pts[i].y);x.stroke()}});x.fillStyle='#ffcf66';pts.forEach((p,i)=>{x.beginPath();x.arc(p.x,p.y,i===0?5:3,0,Math.PI*2);x.fill()});(f.ik_nodes||[]).forEach(k=>{const target=screen(k.target,w,h),pole=screen(k.pole,w,h),root=pts[k.chain[0]];x.strokeStyle='#c084fc';x.beginPath();x.moveTo(root.x,root.y);x.lineTo(pole.x,pole.y);x.stroke();x.fillStyle='#ff6b7a';x.beginPath();x.arc(target.x,target.y,6,0,Math.PI*2);x.fill();x.fillStyle='#c084fc';x.beginPath();x.arc(pole.x,pole.y,4,0,Math.PI*2);x.fill()})}
function drawRoot(){const c=$('rootCanvas'),d=fit(c),x=c.getContext('2d'),w=c.width/d,h=c.height/d;x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,w,h);x.strokeStyle='#ffcf66';x.lineWidth=2;x.beginPath();frames.slice(0,index+1).map(f=>f.root_accumulated.t).forEach((p,i)=>{const px=20+p[0]*35,py=h/2-p[2]*35;i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}
function drawCompression(){const c=$('compressionCanvas'),d=fit(c),x=c.getContext('2d'),w=c.width/d,h=c.height/d,a=trace.compression||{},raw=Math.max(1,a.raw_bytes||1),reduced=a.compressed_bytes||0;x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,w,h);x.fillStyle='#415173';x.fillRect(30,25,w-60,24);x.fillStyle='#6ee7ff';x.fillRect(30,65,(w-60)*reduced/raw,24);x.fillStyle='#e8eefc';x.fillText(`raw ${raw} B`,35,42);x.fillText(`reduced ${reduced} B`,35,82);x.fillText(`errors: ${Number(a.max_translation_error||0).toFixed(4)} pos · ${Number(a.max_rotation_error||0).toFixed(4)} rad · ${Number(a.max_scale_error||0).toFixed(4)} scale`,30,112)}
function chips(id,values){$(id).innerHTML=(values&&values.length)?values.map(v=>`<span class="chip">${esc(v)}</span>`).join(''):'<span class="chip">none</span>'}
function render(){if(!frames.length){$('stats').textContent=trace.error?.message||'No frames';return}const f=frames[index];slider.value=index;$('frameLabel').textContent=`${index+1}/${frames.length} · ${f.ticks} ticks`;drawSkeleton();drawRoot();drawCompression();$('stats').innerHTML=`<b>State</b><span>${esc(f.state)}</span><b>Transition</b><span>${(f.transition*100).toFixed(1)}%</span><b>Blend</b><span>${(f.blend_weights||[]).map(v=>v.toFixed(2)).join(' / ')}</span><b>Cache</b><span>${f.pose_cache.hits} hit · ${f.pose_cache.misses} miss</span><b>IK</b><span>${f.ik.applied?'on':'off'} · error ${f.ik.error.toFixed(4)}</span><b>Eval</b><span>${f.evaluation_us.toFixed(2)} μs</span>`;chips('events',(f.event_occurrences||[]).map(e=>`${e.name}@${e.absolute_tick}`));chips('markers',(f.sync_marker_occurrences||[]).map(m=>`${m.name}@${m.absolute_tick} · n${m.source_node}/c${m.clip}`));const active=new Set(f.executed_nodes||[]);$('graphNodes').innerHTML=((trace.graph_plan&&trace.graph_plan.instructions)||[]).map(n=>`<span class="node ${active.has(n.name)?'active':''}">${esc(n.type)}<small> ${esc(n.name)}</small></span>`).join('');const a=trace.compression||{};$('compression').innerHTML=`<b>Keys</b><span>${a.raw_keys||0} → ${a.compressed_keys||0}</span><b>Bytes</b><span>${a.raw_bytes||0} → ${a.compressed_bytes||0}</span>`}
function toggle(){if(timer){clearInterval(timer);timer=null;$('playPause').textContent='Play'}else if(frames.length){timer=setInterval(()=>{index=(index+1)%frames.length;render()},80);$('playPause').textContent='Pause'}}
$('playPause').onclick=toggle;$('step').onclick=()=>{if(frames.length)index=Math.min(index+1,frames.length-1);render()};slider.oninput=e=>{index=Number(e.target.value);render()};$('spaceMode').onchange=render;addEventListener('resize',render);render();
</script></body></html>)HTML";
  return html;
}

}  // namespace

std::string generate_viewer_html(std::string_view trace_json) {
  return secure_viewer_html(trace_json);
}

Expected<BenchmarkReport, Error> run_benchmark_matrix(DemoBundle& demo) {
  BenchmarkReport report{compiler_name(), os_name(), cpu_name(),
                         std::string{build_git_sha()}, {}};
  const auto append = [&report](Expected<BenchmarkRow, Error> row) -> Expected<void, Error> {
    if (!row) return make_unexpected(row.error());
    report.rows.push_back(std::move(*row));
    return {};
  };
  for (const std::size_t count : {1U, 100U, 1000U}) {
    auto added = append(measure_graph(demo, "complex_graph", count, 1));
    if (!added) return make_unexpected(added.error());
  }
  auto clip_only = append(measure_clip_only(demo));
  if (!clip_only) return make_unexpected(clip_only.error());
  auto compression = append(measure_compressed_vs_raw(demo));
  if (!compression) return make_unexpected(compression.error());
  auto state = append(measure_state_transition(demo));
  if (!state) return make_unexpected(state.error());
  auto ik = append(measure_ik(demo));
  if (!ik) return make_unexpected(ik.error());
  for (const auto& [scenario, count, workers] : {
      std::tuple<std::string, std::size_t, std::size_t>{"serial", 1000, 1},
      {"batch_parallel", 1000, 4}}) {
    auto added = append(measure_graph(demo, scenario, count, workers));
    if (!added) return make_unexpected(added.error());
  }
  return report;
}

std::string benchmark_to_json(const BenchmarkReport& report) {
  std::ostringstream output;
  output << std::setprecision(10) << "{\"git_sha\":\"" << escape_json(report.git_sha)
         << "\",\"compiler\":\"" << escape_json(report.compiler)
         << "\",\"os\":\"" << escape_json(report.operating_system) << "\",\"cpu\":\""
         << escape_json(report.cpu) << "\",\"observation\":\"local machine sample; no cross-machine SLA\",\"rows\":[";
  for (std::size_t index = 0; index < report.rows.size(); ++index) {
    if (index) output << ',';
    const auto& row = report.rows[index];
    output << "{\"scenario\":\"" << escape_json(row.scenario) << "\",\"characters\":"
           << row.characters << ",\"joints\":" << row.joints << ",\"nodes\":" << row.nodes
           << ",\"pose_evaluations\":" << row.pose_evaluations << ",\"total_ns\":"
           << row.total_nanoseconds << ",\"ns_per_character\":" << row.nanoseconds_per_character
           << ",\"ns_per_joint\":" << row.nanoseconds_per_joint << ",\"pose_cache_hit_rate\":"
           << row.pose_cache_hit_rate << ",\"scratch_pose_slots\":" << row.scratch_pose_slots
           << ",\"raw_bytes\":" << row.raw_bytes << ",\"compressed_bytes\":"
           << row.compressed_bytes << ",\"max_translation_error\":" << row.max_translation_error
           << ",\"max_rotation_error\":" << row.max_rotation_error << ",\"max_scale_error\":"
           << row.max_scale_error << ",\"workers\":" << row.worker_count
           << ",\"raw_ns\":" << row.raw_nanoseconds
           << ",\"compressed_ns\":" << row.compressed_nanoseconds << '}';
  }
  output << "]}";
  return output.str();
}

}  // namespace animgraph
