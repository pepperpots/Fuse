cmake_minimum_required(VERSION 3.5.1)
include(CheckIncludeFiles)
project(fuse_runner)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 11)

add_subdirectory(${CMAKE_SOURCE_DIR}/libfuse)

file(GLOB SOURCES "src/*.cpp")
add_executable(fuse_runner ${SOURCES})

target_include_directories(fuse_runner PRIVATE
	libfuseHPM/include
	src/
	external/cxxopts/include
	libfuse/external/spdlog/include
)

target_link_libraries(fuse_runner PRIVATE fuseHPM spdlog)

