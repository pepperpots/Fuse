cmake_minimum_required(VERSION 3.5.1)
include(CheckIncludeFiles)
project(fuseHPM)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "-fpermissive -DCOMPILE_C -fopenmp")
set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(lib_SOURCES
	src/config.cpp
	src/fuse.cpp
	src/target.cpp
	src/profile.cpp
	src/instance.cpp
	src/util.cpp
	src/fuse_types.cpp
	src/profiling.cpp
	src/combination.cpp
	src/statistics.cpp
	src/analysis.cpp
)

if(AFTERMATH_LEGACY)

	# The Aftermath legacy format varies, so these definitions allow user to specify what's included
	# If not set, we assume the trace includes the data structures
	if(NOT DEFINED OS_NUMA_DIST_ENABLED)
	add_definitions(-DOS_NUMA_DIST_ENABLED=1)
	endif()
	if(NOT DEFINED OS_GPU_ENABLED)
	add_definitions(-DOS_GPU_ENABLED=1)
	endif()
	if(NOT DEFINED SYSCALL_ENABLED)
	add_definitions(-DSYSCALL_ENABLED=1)
	endif()

	add_definitions(-DAFTERMATH_LEGACY)
	set(lib_SOURCES ${lib_SOURCES} src/trace_aftermath_legacy.cpp)

	find_library(AFTERMATH_LIB aftermath-core REQUIRED HINTS ${AFTERMATH_LIB_DIR})
	if(NOT (EXISTS "${AFTERMATH_INCLUDE_DIR}/aftermath/core/multi_event_set.h" OR EXISTS "aftermath/core/multi_event_set.h"))
		message(FATAL_ERROR "Cannot find Aftermath headers. Provide non-standard aftermath include directory via the CMake parameter AFTERMATH_INCLUDE_DIR" )
	endif()

else()
	set(lib_SOURCES ${lib_SOURCES} src/trace_aftermath.cpp)
endif()

add_library(fuseHPM SHARED ${lib_SOURCES})

set(SPDLOG_BUILD_SHARED ON CACHE BOOL "Build static lib" FORCE)
add_subdirectory(external/spdlog)
unset(SPDLOG_BUILD_SHARED)

set(BUILD_TESTING OFF CACHE BOOL "Build JSON tests" FORCE)
set(JSON_MultipleHeaders ON CACHE BOOL "Build JSON multiple headers" FORCE) # for forward declaration header
add_subdirectory(external/json)
unset(BUILD_TESTING)
unset(JSON_MultipleHeaders)

find_library(GMP_LIB gmp REQUIRED)
if(NOT GMP_LIB)
	message(FATAL_ERROR "Cannot find libgmp")
endif()

find_library(MPFR_LIB mpfr REQUIRED)
if(NOT MPFR_LIB)
	message(FATAL_ERROR "Cannot find libmpfr")
endif()

target_include_directories(fuseHPM PRIVATE
	external/fast_emd
	external/spdlog/include
	external/json/include
	${AFTERMATH_INCLUDE_DIR}
	include
)

target_link_libraries(fuseHPM spdlog nlohmann_json ${AFTERMATH_LIB} ${GMP_LIB} ${MPFR_LIB})

