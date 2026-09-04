#pragma once

#include "animgraph/compression/compression.hpp"
#include "animgraph/core/expected.hpp"
#include "animgraph/graph/graph.hpp"

#include <vector>

namespace animgraph {

struct DemoBundle {
  CompiledSkeleton skeleton;
  std::vector<AnimationClip> clips;
  GraphDescription graph_description;
  CompiledGraph graph;
  CompressionReport compression;
};

[[nodiscard]] Expected<CompiledSkeleton, Error> make_procedural_humanoid();
[[nodiscard]] Expected<DemoBundle, Error> make_locomotion_demo();

}  // namespace animgraph
