cmake_minimum_required(VERSION 3.10)
project(Cluster CXX)
add_executable(cluster src/main.cpp src/raft.cpp)
find_package(spdlog REQUIRED)
target_link_libraries(cluster PRIVATE spdlog::spdlog)

include(FetchContent)
FetchContent_Declare(
  magic_enum
  GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
  GIT_TAG        v0.9.7 # Replace with the desired release version
)
FetchContent_MakeAvailable(magic_enum)

# Now, link against the imported target
target_link_libraries(cluster PRIVATE magic_enum::magic_enum)