#include "animgraph/core/version.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

ANIMGRAPH_TEST(version_string_is_0_1_0) {
  AG_CHECK_EQ(animgraph::version_string(), std::string_view{"AnimGraphLab 0.1.0"});
  const auto sha = animgraph::build_git_sha();
#if defined(ANIMGRAPH_REQUIRE_BOUND_SHA)
  AG_CHECK_EQ(sha.size(), 40U);
  AG_CHECK(std::ranges::all_of(sha, [](const char value) {
    return std::isdigit(static_cast<unsigned char>(value)) ||
           (value >= 'a' && value <= 'f');
  }));
#else
  AG_CHECK(!sha.empty());
#endif
}
