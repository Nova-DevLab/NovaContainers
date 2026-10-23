nova_add_executable(nova_containers_benchmark benchmark_containers.cpp)

if(WIN32)
	target_compile_definitions(nova_containers_benchmark PRIVATE
		NOMINMAX
		WIN32_LEAN_AND_MEAN
	)
	target_link_libraries(nova_containers_benchmark PRIVATE Psapi)
endif()
