cmake_minimum_required(VERSION 3.10)
project(SafehouseCluster CXX)

include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.x
)
  
FetchContent_MakeAvailable(spdlog)

add_library(safehouse-cluster INTERFACE)
add_executable(cluster src/main.cpp)

target_include_directories(safehouse-cluster INTERFACE include)

# Now, link against the imported targets
target_link_libraries(safehouse-cluster INTERFACE spdlog::spdlog)
target_link_libraries(cluster PRIVATE safehouse-cluster)
