cmake_minimum_required(VERSION 3.20)
project(NovaContainers VERSION 1.1.0 LANGUAGES CXX)

add_library(NovaContainers INTERFACE)
add_library(nova::containers ALIAS NovaContainers)

target_compile_features(NovaContainers INTERFACE cxx_std_20)
target_include_directories(NovaContainers INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

# Architecture requirements are part of the library's usage requirements.
if(MSVC)
	target_compile_options(NovaContainers INTERFACE /arch:AVX2)
else()
	target_compile_options(NovaContainers INTERFACE -mavx2)
endif()

# Embedded consumers should opt in to development executables.
set(nova_standalone OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
	set(nova_standalone ON)
endif()
option(NOVA_BUILD_TESTS "Build NovaContainers tests" ${nova_standalone})
option(NOVA_BUILD_BENCHMARKS "Build NovaContainers benchmarks" ${nova_standalone})

if(NOVA_BUILD_TESTS OR NOVA_BUILD_BENCHMARKS)
	include(cmake/NovaDevelopment.cmake)
endif()
if(NOVA_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
if(NOVA_BUILD_BENCHMARKS)
	add_subdirectory(benchmarks)
endif()
