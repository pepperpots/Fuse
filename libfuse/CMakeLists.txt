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
	src/target.cpp
	src/profile.cpp
	src/instance.cpp
)

find_library(AFTERMATH_LIB aftermath-core REQUIRED HINTS ${AFTERMATH_LIB_DIR})
if(NOT (EXISTS "${AFTERMATH_INCLUDE_DIR}/aftermath/core/multi_event_set.h" OR EXISTS "aftermath/core/multi_event_set.h"))
	message(FATAL_ERROR "Cannot find Aftermath headers. Provide non-standard aftermath include directory via the CMake parameter AFTERMATH_INCLUDE_DIR" )
ENDIF()

set(build_static_lib ON CACHE BOOL "Build static lib" FORCE)
#set(CMAKE_CXX_STANDARD 11)
add_subdirectory(external/easyloggingpp)
unset(build_static_lib)
#set(CMAKE_CXX_STANDARD 17)

target_include_directories(fuse PRIVATE
	external/easyloggingpp/src
	${AFTERMATH_INCLUDE_DIR}
	include
)

target_link_libraries(fuse easyloggingpp ${AFTERMATH_LIB})

