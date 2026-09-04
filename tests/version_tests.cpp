#include "animgraph/core/version.hpp"
#include "test_support.hpp"

#include <string_view>

ANIMGRAPH_TEST(version_string_is_0_1_0) {
  AG_CHECK_EQ(animgraph::version_string(), std::string_view{"AnimGraphLab 0.1.0"});
  AG_CHECK(!animgraph::build_git_sha().empty());
}
