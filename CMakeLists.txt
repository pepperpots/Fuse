cmake_minimum_required(VERSION 3.5.1)
include(CheckIncludeFiles)
project(fuse_runner)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 11)

add_subdirectory(${CMAKE_SOURCE_DIR}/libfuseHPM)
add_subdirectory(${CMAKE_SOURCE_DIR}/external/googletest)

#### Runner implementation

file(GLOB SOURCES "src/*.cpp")
add_executable(fuse_runner ${SOURCES})

target_include_directories(fuse_runner PRIVATE
	libfuseHPM/include
	src/
	external/cxxopts/include
	libfuseHPM/external/spdlog/include
)

target_link_libraries(fuse_runner PRIVATE fuseHPM spdlog)

#### Tests

file(GLOB TEST_SOURCES "test/*.cpp")
add_executable(run_tests ${TEST_SOURCES} ${SOURCES})

set_target_properties(run_tests PROPERTIES EXCLUDE_FROM_ALL TRUE)

target_include_directories(run_tests PRIVATE
	external/googletest/googletest/include
)

target_link_libraries(run_tests PRIVATE fuseHPM spdlog gtest)

file(COPY test/test_data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_custom_target(check ./run_tests DEPENDS run_tests)
