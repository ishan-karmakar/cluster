cmake_minimum_required(VERSION 3.10)
project(SafehouseCluster CXX)
add_library(safehouse-cluster src/raft.cpp)
add_executable(cluster src/main.cpp)
target_include_directories(safehouse-cluster PRIVATE include)
target_include_directories(cluster PRIVATE include)

include(FetchContent)
FetchContent_Declare(
  magic_enum
  GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
  GIT_TAG        v0.9.7 # Replace with the desired release version
)
FetchContent_MakeAvailable(magic_enum)

# Now, link against the imported target
target_link_libraries(safehouse-cluster PRIVATE magic_enum::magic_enum)
target_link_libraries(cluster PRIVATE safehouse-cluster)
