cmake_minimum_required(VERSION 3.10)
project(SafehouseCluster CXX)
include(FetchContent)
FetchContent_Declare(
  magic_enum
  GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
  GIT_TAG master
)
FetchContent_MakeAvailable(magic_enum)

# Add spdlog as an in-tree dependency via FetchContent
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.x
)
FetchContent_MakeAvailable(spdlog)

add_library(safehouse-cluster src/cluster.cpp)
add_executable(cluster src/main.cpp)

target_include_directories(safehouse-cluster PUBLIC include)

# Now, link against the imported targets
target_link_libraries(safehouse-cluster PRIVATE magic_enum::magic_enum spdlog::spdlog)
target_link_libraries(cluster PRIVATE safehouse-cluster)
