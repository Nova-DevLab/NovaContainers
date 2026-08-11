cmake_minimum_required(VERSION 3.20)

if(NOVA_BUILD_TESTS)
  add_executable(nova_containers_tests
    test_hash_map.cpp
  )

  target_link_libraries(nova_containers_tests
    PRIVATE
      NovaContainers
  )

  add_test(NAME nova_containers_tests COMMAND nova_containers_tests)
endif()

if(NOVA_BUILD_BENCHMARKS)
  add_executable(nova_containers_benchmark
    benchmark_hash_map.cpp
  )

  target_link_libraries(nova_containers_benchmark
    PRIVATE
      NovaContainers
  )

  if(WIN32)
    target_compile_definitions(nova_containers_benchmark PRIVATE
      NOMINMAX
      WIN32_LEAN_AND_MEAN
    )
    target_link_libraries(nova_containers_benchmark PRIVATE Psapi)
  endif()
endif()
