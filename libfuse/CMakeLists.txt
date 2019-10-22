cmake_minimum_required(VERSION 3.5.1)
include(CheckIncludeFiles)
project(fuse)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
	message(FATAL_ERROR "Require at least g++-9.0")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-fpermissive -DELPP_THREAD_SAFE -DELPP_FEATURE_CRASH_LOG -DELPP_STACKTRACE_ON_CRASH -DCOMPILE_C -fopenmp")
set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -DELPP_FEATURE_PERFORMANCE_TRACKING -DELPP_PERFORMANCE_MICROSECONDS")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_library(fuse SHARED
	src/config.cpp
	src/fuse.cpp
)

set(build_static_lib ON CACHE BOOL "Build static lib" FORCE)
#set(CMAKE_CXX_STANDARD 11)
add_subdirectory(external/easyloggingpp)
unset(build_static_lib)
#set(CMAKE_CXX_STANDARD 17)

target_include_directories(fuse PRIVATE
	external/easyloggingpp/src
	include
)

target_link_libraries(fuse easyloggingpp)

