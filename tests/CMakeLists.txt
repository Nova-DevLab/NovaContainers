function(nova_add_test target source)
	nova_add_executable(${target} ${source})
	add_test(NAME ${target} COMMAND ${target})
endfunction()

nova_add_test(nova_containers_tests hash/test_hash_map.cpp)
nova_add_test(nova_containers_hash_set_tests hash/test_hash_set.cpp)
nova_add_test(nova_containers_flat_tests flat/test_flat_containers.cpp)
