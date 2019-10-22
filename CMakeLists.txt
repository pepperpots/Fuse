cmake_minimum_required(VERSION 3.5.1)
include(CheckIncludeFiles)
project(fuse_runner)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-fpermissive -DELPP_THREAD_SAFE -DELPP_FEATURE_CRASH_LOG -DELPP_STACKTRACE_ON_CRASH -DCOMPILE_C -fopenmp")
set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -DELPP_FEATURE_PERFORMANCE_TRACKING -DELPP_PERFORMANCE_MICROSECONDS")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_subdirectory(${CMAKE_SOURCE_DIR}/libfuse)

file(GLOB SOURCES "src/*.cpp")
add_executable(fuse_runner ${SOURCES})

target_link_libraries(fuse_runner PRIVATE fuse)

target_include_directories(fuse_runner PRIVATE
	libfuse/include
	src/
	external/cxxopts/include
)
