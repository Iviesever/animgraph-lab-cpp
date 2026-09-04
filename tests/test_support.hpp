#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace animgraph::test {

struct Case {
  std::string_view name;
  void (*function)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

struct Registrar {
  Registrar(std::string_view name, void (*function)()) {
    registry().push_back(Case{name, function});
  }
};

[[noreturn]] inline void fail(std::string_view expression, std::string_view file,
                              int line, std::string_view detail = {}) {
  std::ostringstream message;
  message << file << ':' << line << ": check failed: " << expression;
  if (!detail.empty()) {
    message << " (" << detail << ')';
  }
  throw std::runtime_error(message.str());
}

inline int run_all() {
  std::size_t passed = 0;
  for (const auto& test_case : registry()) {
    try {
      test_case.function();
      ++passed;
      std::cout << "[PASS] " << test_case.name << '\n';
    } catch (const std::exception& error) {
      std::cerr << "[FAIL] " << test_case.name << ": " << error.what() << '\n';
    } catch (...) {
      std::cerr << "[FAIL] " << test_case.name << ": unknown exception\n";
    }
  }
  std::cout << passed << '/' << registry().size() << " tests passed\n";
  return passed == registry().size() ? 0 : 1;
}

}  // namespace animgraph::test

#define ANIMGRAPH_TEST(name)                                                   \
  static void name();                                                         \
  static ::animgraph::test::Registrar name##_registrar{#name, &name};         \
  static void name()

#define AG_CHECK(expression)                                                  \
  do {                                                                        \
    if (!(expression)) {                                                      \
      ::animgraph::test::fail(#expression, __FILE__, __LINE__);               \
    }                                                                         \
  } while (false)

#define AG_CHECK_EQ(actual, expected)                                         \
  do {                                                                        \
    const auto ag_actual = (actual);                                          \
    const auto ag_expected = (expected);                                      \
    if (!(ag_actual == ag_expected)) {                                        \
      ::animgraph::test::fail(#actual " == " #expected, __FILE__, __LINE__);  \
    }                                                                         \
  } while (false)

#define AG_CHECK_NEAR(actual, expected, tolerance)                            \
  do {                                                                        \
    const auto ag_actual = (actual);                                          \
    const auto ag_expected = (expected);                                      \
    const auto ag_tolerance = (tolerance);                                    \
    if (std::abs(ag_actual - ag_expected) > ag_tolerance) {                   \
      ::animgraph::test::fail(#actual " ~= " #expected, __FILE__, __LINE__); \
    }                                                                         \
  } while (false)
