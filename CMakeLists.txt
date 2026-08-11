cmake_minimum_required(VERSION 3.20)
project(NovaContainers VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Target configuration (Header-only library)
add_library(NovaContainers INTERFACE)
add_library(nova::containers ALIAS NovaContainers)

target_include_directories(NovaContainers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

# Compiler flags
if(MSVC)
  target_compile_options(NovaContainers INTERFACE
    /W4
    /permissive-
    /arch:AVX2
  )
else()
  target_compile_options(NovaContainers INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -mavx2
  )
endif()

# Enable testing
option(NOVA_BUILD_TESTS "Build NovaContainers tests" ON)
option(NOVA_BUILD_BENCHMARKS "Build NovaContainers benchmarks" ON)

if(NOVA_BUILD_TESTS)
  enable_testing()
endif()

if(NOVA_BUILD_TESTS OR NOVA_BUILD_BENCHMARKS)
  add_subdirectory(tests)
endif()
