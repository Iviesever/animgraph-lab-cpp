#include "animgraph/core/version.hpp"

namespace animgraph {

#ifndef ANIMGRAPH_GIT_SHA
#define ANIMGRAPH_GIT_SHA "unbound-mqb"
#endif

std::string_view version_string() noexcept { return "AnimGraphLab 0.1.0"; }
std::string_view build_git_sha() noexcept { return ANIMGRAPH_GIT_SHA; }

}  // namespace animgraph
