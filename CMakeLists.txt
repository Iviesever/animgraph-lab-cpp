cmake_minimum_required(VERSION 3.25)
project(AnimGraphLab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(cmake/AnimGraphSources.cmake)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_library(animgraph STATIC ${ANIMGRAPH_SOURCES})
target_include_directories(animgraph PUBLIC include)

add_executable(animgraph_lab src/cli/main.cpp)
target_link_libraries(animgraph_lab PRIVATE animgraph)

add_executable(animc src/tools/animc.cpp)
target_link_libraries(animc PRIVATE animgraph)

add_executable(animgraph_tests ${ANIMGRAPH_TEST_SOURCES})
target_include_directories(animgraph_tests PRIVATE include tests)
target_link_libraries(animgraph_tests PRIVATE animgraph)

add_test(NAME animgraph_tests COMMAND animgraph_tests)

add_executable(animgraph_property tests/property/property.cpp)
target_link_libraries(animgraph_property PRIVATE animgraph)
add_test(NAME animgraph_property COMMAND animgraph_property)

add_executable(animgraph_fuzz tests/fuzz/fuzz.cpp)
target_link_libraries(animgraph_fuzz PRIVATE animgraph)
add_test(NAME animgraph_fuzz COMMAND animgraph_fuzz)

option(ANIMGRAPH_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(ANIMGRAPH_ENABLE_SANITIZERS AND NOT MSVC)
  foreach(target animgraph animgraph_lab animc animgraph_tests animgraph_property animgraph_fuzz)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()
