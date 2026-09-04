#include "animgraph/asset/tool.hpp"
#include "animgraph/core/version.hpp"
#include "test_support.hpp"

#include <array>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

ANIMGRAPH_TEST(animc_compiles_inspects_and_validates_real_asset_files) {
  namespace fs = std::filesystem;
  const fs::path output = fs::path{"artifacts"} / "tests" / "tool-root.agskel";
  const std::string output_text = output.string();
  fs::create_directories(output.parent_path());
  std::ostringstream standard_output;
  std::ostringstream standard_error;

  const std::array version_args{std::string_view{"--version"}};
  AG_CHECK_EQ(animgraph::run_asset_tool(version_args, standard_output, standard_error), 0);
  AG_CHECK(standard_output.str().find("animc sha=" +
      std::string{animgraph::build_git_sha()}) != std::string::npos);

  standard_output.str({});
  const std::array compile_args{std::string_view{"compile"}, std::string_view{"skeleton"},
                                std::string_view{output_text}};
  AG_CHECK_EQ(animgraph::run_asset_tool(compile_args, standard_output, standard_error), 0);
  AG_CHECK(fs::is_regular_file(output));

  standard_output.str({});
  const std::array inspect_args{std::string_view{"inspect"}, std::string_view{output_text}};
  AG_CHECK_EQ(animgraph::run_asset_tool(inspect_args, standard_output, standard_error), 0);
  AG_CHECK(standard_output.str().find("\"kind\":\"skeleton\"") != std::string::npos);

  standard_output.str({});
  const std::array validate_args{std::string_view{"validate"}, std::string_view{output_text}};
  AG_CHECK_EQ(animgraph::run_asset_tool(validate_args, standard_output, standard_error), 0);
  AG_CHECK(standard_output.str().find("valid") != std::string::npos);

  fs::remove(output);
}
