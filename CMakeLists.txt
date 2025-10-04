cmake_minimum_required(VERSION 3.21)
project(SafehouseCluster CXX)

include(FetchContent)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.x
)
  
FetchContent_MakeAvailable(spdlog)

add_library(safehouse-cluster INTERFACE)

target_include_directories(safehouse-cluster INTERFACE include)
target_link_libraries(safehouse-cluster INTERFACE spdlog::spdlog)

if (PROJECT_IS_TOP_LEVEL)
	add_executable(cluster src/main.cpp)
	target_link_libraries(cluster safehouse-cluster)
endif()
