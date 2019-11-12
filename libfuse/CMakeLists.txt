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
set(CMAKE_CXX_FLAGS "-fpermissive -DCOMPILE_C -fopenmp")
set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

add_library(fuse SHARED
	src/config.cpp
	src/fuse.cpp
	src/target.cpp
	src/profile.cpp
	src/instance.cpp
	src/util.cpp
	src/fuse_types.cpp
	src/profiling.cpp
)

find_library(AFTERMATH_LIB aftermath-core REQUIRED HINTS ${AFTERMATH_LIB_DIR})
if(NOT (EXISTS "${AFTERMATH_INCLUDE_DIR}/aftermath/core/multi_event_set.h" OR EXISTS "aftermath/core/multi_event_set.h"))
	message(FATAL_ERROR "Cannot find Aftermath headers. Provide non-standard aftermath include directory via the CMake parameter AFTERMATH_INCLUDE_DIR" )
ENDIF()

set(SPDLOG_BUILD_SHARED ON CACHE BOOL "Build static lib" FORCE)
add_subdirectory(external/spdlog)
unset(SPDLOG_BUILD_SHARED)

set(BUILD_TESTING OFF CACHE BOOL "Build JSON tests" FORCE)
set(JSON_MultipleHeaders ON CACHE BOOL "Build JSON multiple headers" FORCE) # for forward declaration header
add_subdirectory(external/json)
unset(BUILD_TESTING)
unset(JSON_MultipleHeaders)

target_include_directories(fuse PRIVATE
	external/spdlog/include
	external/json/include
	${AFTERMATH_INCLUDE_DIR}
	include
)

target_link_libraries(fuse spdlog nlohmann_json ${AFTERMATH_LIB})

