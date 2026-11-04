nova_add_executable(nova_containers_benchmark benchmark_containers.cpp)

if(WIN32)
	target_compile_definitions(nova_containers_benchmark PRIVATE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
	)
	target_link_libraries(nova_containers_benchmark PRIVATE Psapi)
endif()

nova_add_executable(nova_containers_list_benchmark list/benchmark_list.cpp)

nova_add_executable(nova_containers_list_iteration_benchmark list/benchmark_list_iteration.cpp)

nova_add_executable(nova_containers_list_optimize_benchmark list/benchmark_list_optimize.cpp)

nova_add_executable(nova_containers_list_memory_benchmark list/benchmark_list_memory.cpp)
set(NOVA_LIST_MEMORY_BASELINE_HEADER "" CACHE FILEPATH "Isolated nova_baseline list header for memory A/B")
set(NOVA_LIST_MEMORY_CANDIDATE_HEADER "" CACHE FILEPATH "Optional candidate list header for memory A/B")
if(NOVA_LIST_MEMORY_BASELINE_HEADER)
	target_compile_definitions(nova_containers_list_memory_benchmark PRIVATE
		NOVA_LIST_BASELINE_HEADER="${NOVA_LIST_MEMORY_BASELINE_HEADER}")
endif()
if(NOVA_LIST_MEMORY_CANDIDATE_HEADER)
	target_compile_definitions(nova_containers_list_memory_benchmark PRIVATE
		NOVA_LIST_CANDIDATE_HEADER="${NOVA_LIST_MEMORY_CANDIDATE_HEADER}")
endif()
option(NOVA_LIST_MEMORY_NATIVE_HEAP "Disable allocation wrappers for native heap timing" OFF)
if(NOVA_LIST_MEMORY_NATIVE_HEAP)
	target_compile_definitions(nova_containers_list_memory_benchmark PRIVATE NOVA_MEMORY_NATIVE_HEAP)
endif()
