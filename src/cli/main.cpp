#include "animgraph/app/lab.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string_view> arguments;
  arguments.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
  for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
  return animgraph::run_lab(arguments, std::cout, std::cerr);
}
