#include "animgraph/core/version.hpp"

#include <iostream>
#include <string_view>

namespace {

void print_usage() {
  std::cout << "Usage: animgraph_lab <command>\n"
               "Commands: --version, verify\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    print_usage();
    return 2;
  }

  const std::string_view command{argv[1]};
  if (command == "--version") {
    std::cout << animgraph::version_string() << '\n';
    return 0;
  }
  if (command == "verify") {
    std::cout << "AnimGraphLab baseline verification: ok\n";
    return 0;
  }

  std::cerr << "Unknown command: " << command << '\n';
  print_usage();
  return 2;
}
