#pragma once

#include <string_view>

namespace animgraph {

[[nodiscard]] std::string_view version_string() noexcept;
[[nodiscard]] std::string_view build_git_sha() noexcept;

}  // namespace animgraph
