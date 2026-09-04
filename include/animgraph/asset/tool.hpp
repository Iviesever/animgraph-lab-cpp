#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace animgraph {

int run_asset_tool(std::span<const std::string_view> arguments,
                   std::ostream& output, std::ostream& error);

}  // namespace animgraph
