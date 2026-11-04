#include <chrono>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "NovaContainers/nova_containers.hpp"

void require(bool condition) {
	if (!condition) {
		throw std::runtime_error("test requirement failed");
	}
}

void require_differential(bool condition, uint32_t seed, int operation,
					 const char* check) {
	if (!condition) {
		throw std::runtime_error(
			"differential failure: seed=" + std::to_string(seed) +
			", operation=" + std::to_string(operation) + ", check=" + check);
	}
}

void test_basic_operations() {
  std::cout << "[Test 1] Basic Operations (Insert, Find, Erase, operator[])... " << std::flush;

  nova::hash_map<std::string, int> map;

  require(map.empty());
  require(map.size() == 0);

  // Insert test
  auto [it1, inserted1] = map.insert({"apple", 100});
  require(inserted1);
  require(it1->first == "apple");
  require(it1->second == 100);
  require(map.size() == 1);

  // Duplicate insert test
  auto [it2, inserted2] = map.insert({"apple", 200});
  require(!inserted2);
  require(it2->second == 100);
  require(map.size() == 1);

  // operator[] test
  map["banana"] = 200;
  map["cherry"] = 300;
  require(map.size() == 3);
  require(map["apple"] == 100);
  require(map["banana"] == 200);
  require(map["cherry"] == 300);

  // Find test
  auto find_it = map.find("banana");
  require(find_it != map.end());
  require(find_it->second == 200);

  auto not_found_it = map.find("durian");
  require(not_found_it == map.end());

  // Erase test
  bool erased = map.erase("banana");
  require(erased);
  require(map.size() == 2);
  require(map.find("banana") == map.end());

  bool erase_again = map.erase("banana");
  require(!erase_again);

  std::cout << "PASSED!\n";
}

void test_large_scale_rehash() {
  std::cout << "[Test 2] Large-scale Insert & Rehash (100,000 items)... " << std::flush;

  constexpr int k_count = 100000;
  nova::hash_map<int, int> map;

  for (int i = 0; i < k_count; ++i) {
    map[i] = i * 2;
  }

  require(map.size() == k_count);

  for (int i = 0; i < k_count; ++i) {
    auto it = map.find(i);
    require(it != map.end());
    require(it->second == i * 2);
  }

  // Erase half
  for (int i = 0; i < k_count; i += 2) {
    bool erased = map.erase(i);
    require(erased);
  }

  require(map.size() == k_count / 2);

  for (int i = 0; i < k_count; ++i) {
    auto it = map.find(i);
    if (i % 2 == 0) {
      require(it == map.end());
    } else {
      require(it != map.end());
      require(it->second == i * 2);
    }
  }

  std::cout << "PASSED!\n";
}

void test_iteration() {
  std::cout << "[Test 3] Iteration & Range-based For Loop... " << std::flush;

  nova::hash_map<std::string, int> map = {
      {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}};

  int sum = 0;
  int count = 0;
  for (const auto& [key, value] : map) {
    sum += value;
    ++count;
  }

  require(count == 4);
  require(sum == 10);

  std::cout << "PASSED!\n";
}

struct throwing_hash {
	std::size_t operator()(int key) const {
		if (key == 42) {
			throw std::runtime_error("hash failure");
		}
		return std::hash<int>{}(key);
	}
};

struct toggle_throw_hash {
	std::size_t operator()(int key) const {
		if (should_throw) {
			throw std::runtime_error("rehash hash failure");
		}
		return std::hash<int>{}(key);
	}

	inline static bool should_throw = false;
};

struct collision_hash {
	std::size_t operator()(int) const noexcept { return 0; }
};

struct counting_hash {
	std::size_t operator()(int key) const noexcept {
		++calls;
		return std::hash<int>{}(key);
	}

	inline static std::size_t calls = 0;
};

struct throw_on_construction {
	explicit throw_on_construction(int initial_value) : value(initial_value) {
		if (should_throw) {
			throw std::runtime_error("construction failure");
		}
	}

	inline static bool should_throw = false;
	int value;
};

struct throwing_equality {
	bool operator()(int left, int right) const {
		if (should_throw) {
			throw std::runtime_error("equality failure");
		}
		return left == right;
	}

	inline static bool should_throw = false;
};

struct seeded_hash {
	seeded_hash() : seed(next_seed++ * 0x9E3779B97F4A7C15ULL) {}

	std::size_t operator()(int key) const noexcept {
		return std::hash<int>{}(key) ^ seed;
	}

	inline static std::size_t next_seed = 1;
	std::size_t seed;
};

struct copy_throwing_value {
	explicit copy_throwing_value(int initial_value) : value(initial_value) {}

	copy_throwing_value(const copy_throwing_value& other) : value(other.value) {
		if (throw_on_copy) {
			throw std::runtime_error("copy failure");
		}
	}

	copy_throwing_value(copy_throwing_value&&) noexcept = default;
	copy_throwing_value& operator=(const copy_throwing_value&) = default;
	copy_throwing_value& operator=(copy_throwing_value&&) noexcept = default;

	inline static bool throw_on_copy = false;
	int value;
};

struct throw_on_move {
	explicit throw_on_move(int initial_value) : value(initial_value) {}

	throw_on_move(const throw_on_move&) = delete;
	throw_on_move& operator=(const throw_on_move&) = delete;

	throw_on_move(throw_on_move&& other) : value(other.value) {
		if (moves_before_throw == 0) {
			throw std::runtime_error("move failure");
		}
		if (moves_before_throw > 0) {
			--moves_before_throw;
		}
		other.value = -1;
	}

	throw_on_move& operator=(throw_on_move&&) = delete;

	inline static int moves_before_throw = -1;
	int value;
};

struct tail_probe_hash {
	std::size_t operator()(int) const noexcept {
		return nova::containers::detail::group_type::k_width - 1;
	}
};

struct construction_counter {
	explicit construction_counter(int initial_value) : value(initial_value) {
		++construction_count;
	}

	construction_counter(const construction_counter& other) : value(other.value) {
		++construction_count;
	}
	construction_counter(construction_counter&& other) noexcept : value(other.value) {
		++construction_count;
	}
	construction_counter& operator=(const construction_counter&) = default;
	construction_counter& operator=(construction_counter&&) noexcept = default;

	inline static int construction_count = 0;
	int value;
};

struct allocator_stats {
	std::size_t allocation_count = 0;
	std::size_t deallocation_count = 0;
	std::size_t allocated_bytes = 0;
	std::size_t deallocated_bytes = 0;
	std::size_t current_allocated_bytes = 0;
	std::size_t peak_allocated_bytes = 0;
};

template <typename T, bool k_propagate = false>
class tracking_allocator {
 public:
	using value_type = T;
	using propagate_on_container_copy_assignment =
		std::bool_constant<k_propagate>;
	using propagate_on_container_move_assignment =
		std::bool_constant<k_propagate>;
	using propagate_on_container_swap = std::bool_constant<k_propagate>;
	using is_always_equal = std::false_type;

	template <typename U>
	struct rebind {
		using other = tracking_allocator<U, k_propagate>;
	};

	tracking_allocator() noexcept = default;
	explicit tracking_allocator(allocator_stats* stats) noexcept : stats_(stats) {}

	template <typename U>
	tracking_allocator(const tracking_allocator<U, k_propagate>& other) noexcept
		: stats_(other.stats()) {}

	[[nodiscard]] T* allocate(std::size_t count) {
		if (stats_ != nullptr) {
			++stats_->allocation_count;
			stats_->allocated_bytes += count * sizeof(T);
			stats_->current_allocated_bytes += count * sizeof(T);
			if (stats_->current_allocated_bytes > stats_->peak_allocated_bytes) {
				stats_->peak_allocated_bytes = stats_->current_allocated_bytes;
			}
		}
		return std::allocator<T>{}.allocate(count);
	}

	void deallocate(T* pointer, std::size_t count) noexcept {
		if (stats_ != nullptr) {
			++stats_->deallocation_count;
			stats_->deallocated_bytes += count * sizeof(T);
			stats_->current_allocated_bytes -= count * sizeof(T);
		}
		std::allocator<T>{}.deallocate(pointer, count);
	}

	[[nodiscard]] allocator_stats* stats() const noexcept { return stats_; }

	template <typename U>
	[[nodiscard]] bool operator==(
		const tracking_allocator<U, k_propagate>& other) const noexcept {
		return stats_ == other.stats();
	}

 private:
	allocator_stats* stats_ = nullptr;
};

template <typename T>
class throwing_move_allocator {
 public:
	using value_type = T;
	using propagate_on_container_move_assignment = std::true_type;
	using is_always_equal = std::false_type;

	template <typename U>
	struct rebind {
		using other = throwing_move_allocator<U>;
	};

	throwing_move_allocator() noexcept = default;
	explicit throwing_move_allocator(allocator_stats* stats) noexcept : stats_(stats) {}
	throwing_move_allocator(const throwing_move_allocator&) noexcept = default;
	throwing_move_allocator(throwing_move_allocator&&) noexcept = default;

	template <typename U>
	throwing_move_allocator(const throwing_move_allocator<U>& other) noexcept
		: stats_(other.stats()) {}

	throwing_move_allocator& operator=(const throwing_move_allocator&) noexcept = default;
	throwing_move_allocator& operator=(throwing_move_allocator&& other) {
		if (throw_on_move_assignment) {
			throw std::runtime_error("allocator move assignment failure");
		}
		stats_ = other.stats_;
		return *this;
	}

	[[nodiscard]] T* allocate(std::size_t count) {
		if (stats_ != nullptr) {
			++stats_->allocation_count;
		}
		return std::allocator<T>{}.allocate(count);
	}

	void deallocate(T* pointer, std::size_t count) noexcept {
		if (stats_ != nullptr) {
			++stats_->deallocation_count;
		}
		std::allocator<T>{}.deallocate(pointer, count);
	}

	[[nodiscard]] allocator_stats* stats() const noexcept { return stats_; }

	template <typename U>
	[[nodiscard]] bool operator==(
		const throwing_move_allocator<U>& other) const noexcept {
		return stats_ == other.stats();
	}

	inline static bool throw_on_move_assignment = false;

 private:
	allocator_stats* stats_ = nullptr;
};

struct reserved_table_measurement {
	std::size_t capacity = 0;
	std::size_t slot_bytes_per_bucket = 0;
	std::size_t control_bytes = 0;
	std::size_t table_bytes = 0;
};

template <typename Key, typename Value>
reserved_table_measurement measure_reserved_table(std::size_t element_count) {
	using pair_type = std::pair<const Key, Value>;
	using allocator_type = tracking_allocator<pair_type>;
	using map_type = nova::hash_map<Key, Value, std::hash<Key>, std::equal_to<Key>,
						   allocator_type>;

	allocator_stats stats;
	reserved_table_measurement measurement;
	{
		map_type map(0, {}, {}, allocator_type(&stats));
		map.reserve(element_count);
		require(map.capacity() >= element_count);
		require(stats.allocation_count == 1);
		require(stats.allocated_bytes % map.capacity() == 0);

		measurement.capacity = map.capacity();
		measurement.slot_bytes_per_bucket =
			stats.allocated_bytes / map.capacity();
		measurement.control_bytes =
			map.capacity() + nova::containers::detail::group_type::k_width;
		measurement.table_bytes = stats.allocated_bytes + measurement.control_bytes;
	}

	require(stats.allocation_count == stats.deallocation_count);
	require(stats.allocated_bytes == stats.deallocated_bytes);
	require(stats.current_allocated_bytes == 0);
	return measurement;
}

using tracked_string =
	std::basic_string<char, std::char_traits<char>, tracking_allocator<char>>;

struct tracked_string_hash {
	std::size_t operator()(const tracked_string& value) const noexcept {
		return std::hash<std::string_view>{}(
			std::string_view(value.data(), value.size()));
	}
};

struct transparent_string_hash {
	using is_transparent = void;

	std::size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
};

struct transparent_string_equal {
	using is_transparent = void;

	bool operator()(std::string_view left, std::string_view right) const noexcept {
		return left == right;
	}
};

void test_const_lookup_and_bounds_checking() {
	std::cout << "[Test 4] Const lookup, contains, and at... " << std::flush;

	const nova::hash_map<std::string, int> map = {{"answer", 42}};
	const auto iterator = map.find("answer");
	require(iterator != map.end());
	require(iterator->second == 42);
	require(map.contains("answer"));
	require(!map.contains("missing"));
	require(map.at("answer") == 42);

	bool did_throw = false;
	try {
		static_cast<void>(map.at("missing"));
	} catch (const std::out_of_range&) {
		did_throw = true;
	}
	require(did_throw);

	std::cout << "PASSED!\n";
}

void test_hash_exceptions_propagate() {
	std::cout << "[Test 5] Hash exceptions propagate... " << std::flush;

	nova::hash_map<int, int, throwing_hash> map;
	map.insert({1, 10});
	bool did_throw = false;
	try {
		static_cast<void>(map.find(42));
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	require(did_throw);

	std::cout << "PASSED!\n";
}

void test_equality_exception_preserves_rehash_source() {
	std::cout << "[Test 6] Equality exception preserves rehash source... "
			<< std::flush;

	nova::hash_map<int, int, collision_hash, throwing_equality> map;
	map.insert({1, 10});
	map.insert({2, 20});
	const std::size_t original_capacity = map.capacity();

	throwing_equality::should_throw = true;
	bool did_throw = false;
	try {
		map.rehash(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	throwing_equality::should_throw = false;

	require(did_throw);
	require(map.capacity() == original_capacity);
	require(map.size() == 2);
	require(map.at(1) == 10);
	require(map.at(2) == 20);

	std::cout << "PASSED!\n";
}

void test_stateful_hash_survives_copy_and_move() {
	std::cout << "[Test 7] Stateful hash survives copy and move... "
			  << std::flush;

	nova::hash_map<int, int, seeded_hash> source;
	source.insert({7, 70});
	source.insert({11, 110});

	nova::hash_map<int, int, seeded_hash> copied(source);
	require(copied.at(7) == 70);
	require(copied.at(11) == 110);

	nova::hash_map<int, int, seeded_hash> moved(std::move(source));
	require(moved.at(7) == 70);
	require(moved.at(11) == 110);

	std::cout << "PASSED!\n";
}

void test_rehash_preserves_table_when_copy_throws() {
	std::cout << "[Test 8] Failed rehash preserves the original table... "
			  << std::flush;

	nova::hash_map<int, copy_throwing_value> map;
	map.emplace(1, copy_throwing_value(10));
	map.emplace(2, copy_throwing_value(20));
	const std::size_t original_capacity = map.capacity();

	copy_throwing_value::throw_on_copy = true;
	bool did_throw = false;
	try {
		map.reserve(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	copy_throwing_value::throw_on_copy = false;

	require(did_throw);
	require(map.capacity() == original_capacity);
	require(map.size() == 2);
	require(map.at(1).value == 10);
	require(map.at(2).value == 20);

	std::cout << "PASSED!\n";
}

void test_move_only_rehash_preserves_structural_invariants() {
	std::cout << "[Test 9] Move-only rehash keeps structural invariants... "
			<< std::flush;

	throw_on_move::moves_before_throw = -1;
	nova::hash_map<int, throw_on_move> map;
	map.try_emplace(1, 10);
	map.try_emplace(2, 20);
	const std::size_t original_capacity = map.capacity();

	throw_on_move::moves_before_throw = 1;
	bool did_throw = false;
	try {
		map.rehash(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	throw_on_move::moves_before_throw = -1;

	require(did_throw);
	require(map.capacity() == original_capacity);
	require(map.size() == 2);
	require(map.find(1) != map.end());
	require(map.find(2) != map.end());
	require(map.try_emplace(3, 30).second);
	require(map.at(3).value == 30);

	std::cout << "PASSED!\n";
}

void test_tombstones_are_compacted_without_growth() {
	std::cout << "[Test 10] Tombstones compact without capacity growth... "
			  << std::flush;

	nova::hash_map<int, int> map;
	constexpr int k_growth_limit =
		static_cast<int>(nova::containers::detail::group_type::k_width * 7 / 8);
	constexpr int k_erase_count = k_growth_limit / 2;
	for (int i = 0; i < k_growth_limit; ++i) {
		map.insert({i, i * 10});
	}
	require(map.capacity() == nova::containers::detail::group_type::k_width);

	for (int i = 0; i < k_erase_count; ++i) {
		require(map.erase(i));
	}
	map.insert({100, 1000});

	require(map.capacity() == nova::containers::detail::group_type::k_width);
	require(map.size() == k_growth_limit - k_erase_count + 1);
	for (int i = k_erase_count; i < k_growth_limit; ++i) {
		require(map.at(i) == i * 10);
	}
	require(map.at(100) == 1000);

	std::cout << "PASSED!\n";
}

void test_control_tail_wrap_around() {
	std::cout << "[Test 11] Control tail wrap-around probing... " << std::flush;

	nova::hash_map<int, int, tail_probe_hash> map;
	constexpr int k_growth_limit =
		static_cast<int>(nova::containers::detail::group_type::k_width * 7 / 8);
	for (int i = 0; i < k_growth_limit; ++i) {
		require(map.insert({i, i * 10}).second);
	}
	require(map.capacity() == nova::containers::detail::group_type::k_width);

	require(map.erase(1));
	require(map.find(1) == map.end());
	for (int i = 2; i < k_growth_limit; ++i) {
		require(map.at(i) == i * 10);
	}

	require(map.insert({100, 1000}).second);
	require(map.at(100) == 1000);
	for (int i = 2; i < k_growth_limit; ++i) {
		require(map.at(i) == i * 10);
	}

	std::cout << "PASSED!\n";
}

void test_extended_container_api() {
	std::cout << "[Test 12] Extended container API... " << std::flush;

	construction_counter::construction_count = 0;
	nova::hash_map<int, construction_counter> map;
	auto [first, first_inserted] = map.try_emplace(7, 70);
	require(first_inserted);
	require(first->second.value == 70);
	const int construction_count_after_first =
		construction_counter::construction_count;
	require(construction_count_after_first > 0);

	auto [duplicate, duplicate_inserted] = map.try_emplace(7, 700);
	require(!duplicate_inserted);
	require(duplicate->second.value == 70);
	require(construction_counter::construction_count == construction_count_after_first);

	auto [assigned, assigned_inserted] =
		map.insert_or_assign(7, construction_counter(71));
	require(!assigned_inserted);
	require(assigned->second.value == 71);
	require(map.count(7) == 1);
	require(map.count(8) == 0);
	require(map.load_factor() > 0.0F);
	require(map.max_load_factor() == 0.875F);

	map.try_emplace(8, 80);
	const nova::hash_map<int, construction_counter>::const_iterator const_it =
		map.begin();
	const int erased_key = const_it->first;
	auto next = map.erase(const_it);
	require(next == map.end() || next->first != erased_key);
	require(map.size() == 1);

	map.reserve(15);
	require(map.capacity() >= 32);
	map.rehash(16);
	require(map.capacity() >= nova::containers::detail::group_type::k_width);

	std::cout << "PASSED!\n";
}

void test_differential_operations() {
	std::cout << "[Test 13] Differential operations against std::unordered_map... "
			<< std::flush;

	constexpr int k_operation_count = 100000;
	constexpr int k_key_range = 257;
	uint32_t state = 0xC0FFEEU;
	auto next_random = [&state]() {
		state = state * 1664525U + 1013904223U;
		return state;
	};

	nova::hash_map<int, int> nova_map;
	std::unordered_map<int, int> std_map;
	for (int operation = 0; operation < k_operation_count; ++operation) {
		const int key = static_cast<int>(next_random() % k_key_range);
		const int value = static_cast<int>(next_random());
		switch (next_random() % 4U) {
			case 0: {
				auto [nova_it, nova_inserted] = nova_map.try_emplace(key, value);
				auto [std_it, std_inserted] = std_map.try_emplace(key, value);
				require(nova_inserted == std_inserted);
				require(nova_it->second == std_it->second);
				break;
			}
			case 1: {
				auto [nova_it, nova_inserted] =
					nova_map.insert_or_assign(key, value);
				auto [std_it, std_inserted] =
					std_map.insert_or_assign(key, value);
				require(nova_inserted == std_inserted);
				require(nova_it->second == std_it->second);
				break;
			}
			case 2:
				require(nova_map.erase(key) == (std_map.erase(key) != 0));
				break;
			default: {
				const auto nova_it = nova_map.find(key);
				const auto std_it = std_map.find(key);
				require((nova_it == nova_map.end()) == (std_it == std_map.end()));
				if (nova_it != nova_map.end()) {
					require(nova_it->second == std_it->second);
				}
				break;
			}
		}

		require(nova_map.size() == std_map.size());
	}

	for (const auto& [key, value] : std_map) {
		const auto nova_it = nova_map.find(key);
		require(nova_it != nova_map.end());
		require(nova_it->second == value);
	}

	std::cout << "PASSED!\n";
}

void test_collision_stress_operations() {
	std::cout << "[Test 14] Collision stress against std::unordered_map... "
			<< std::flush;

	using nova_map_type = nova::hash_map<int, int, collision_hash>;
	using std_map_type = std::unordered_map<int, int, collision_hash>;
	constexpr uint32_t k_seeds[] = {0x12345678U, 0x9ABCDEF0U, 0xCAFEBABEU};
	constexpr int k_operation_count = 50000;
	constexpr int k_key_range = 64;

	for (const uint32_t seed : k_seeds) {
		uint32_t state = seed;
		auto next_random = [&state]() {
			state = state * 1664525U + 1013904223U;
			return state;
		};

		nova_map_type nova_map;
		std_map_type std_map;
		for (int operation = 0; operation < k_operation_count; ++operation) {
			const int key = static_cast<int>(next_random() % k_key_range);
			const int value = static_cast<int>(next_random());
			switch (next_random() % 7U) {
				case 0: {
					auto [nova_it, nova_inserted] =
						nova_map.insert_or_assign(key, value);
					auto [std_it, std_inserted] =
						std_map.insert_or_assign(key, value);
					require_differential(nova_inserted == std_inserted, seed,
						operation, "insert_or_assign result");
					require_differential(nova_it->second == std_it->second, seed,
						operation, "insert_or_assign value");
					break;
				}
				case 1: {
					auto [nova_it, nova_inserted] =
						nova_map.try_emplace(key, value);
					auto [std_it, std_inserted] =
						std_map.try_emplace(key, value);
					require_differential(nova_inserted == std_inserted, seed,
						operation, "try_emplace result");
					require_differential(nova_it->second == std_it->second, seed,
						operation, "try_emplace value");
					break;
				}
				case 2:
					require_differential(nova_map.erase(key) == (std_map.erase(key) != 0),
						seed, operation, "erase result");
					break;
				case 3: {
					const std::size_t requested_size = next_random() % 256U;
					nova_map.reserve(requested_size);
					std_map.reserve(requested_size);
					break;
				}
				case 4: {
					const std::size_t requested_buckets =
						16 + next_random() % 256U;
					nova_map.rehash(requested_buckets);
					std_map.rehash(requested_buckets);
					break;
				}
				case 5: {
					nova_map_type copied(nova_map);
					nova_map_type moved(std::move(copied));
					nova_map_type reassigned;
					reassigned = std::move(moved);
					nova_map = reassigned;
					break;
				}
				default: {
					const auto nova_it = nova_map.find(key);
					const auto std_it = std_map.find(key);
					require_differential(
						(nova_it == nova_map.end()) == (std_it == std_map.end()), seed,
						operation, "find presence");
					if (nova_it != nova_map.end()) {
						require_differential(nova_it->second == std_it->second, seed,
							operation, "find value");
					}
					break;
				}
			}

			require_differential(nova_map.size() == std_map.size(), seed, operation,
				"size");
			if (operation % 127 == 0) {
				for (const auto& [std_key, std_value] : std_map) {
					const auto nova_it = nova_map.find(std_key);
					require_differential(nova_it != nova_map.end(), seed, operation,
						"full comparison presence");
					require_differential(nova_it->second == std_value, seed, operation,
						"full comparison value");
				}
			}
		}
	}

	std::cout << "PASSED!\n";
}

void test_memory_footprint_accounting() {
	std::cout << "[Test 15] Memory footprint accounting... " << std::flush;

	using pair_type = std::pair<const int, int>;
	using allocator_type = tracking_allocator<pair_type>;
	using map_type = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   allocator_type>;

	constexpr std::size_t k_element_count = 100000;
	allocator_stats stats;
	{
		map_type map(0, {}, {}, allocator_type(&stats));
		require(map.capacity() == 0);
		require(stats.allocation_count == 0);

		map.reserve(k_element_count);
		require(map.capacity() >= k_element_count);
		require(stats.allocation_count == 1);
		require(stats.allocated_bytes % map.capacity() == 0);

		const std::size_t slot_bytes_per_bucket =
			stats.allocated_bytes / map.capacity();
		const std::size_t control_bytes =
			map.capacity() + nova::containers::detail::group_type::k_width;
		const std::size_t table_bytes = stats.allocated_bytes + control_bytes;
		const std::size_t original_capacity = map.capacity();
		const std::size_t value_type_bytes = sizeof(pair_type);
		const std::size_t map_object_bytes = sizeof(map_type);
		map.rehash(original_capacity * 2);
		const std::size_t rehashed_control_bytes =
			original_capacity + nova::containers::detail::group_type::k_width +
			map.capacity() + nova::containers::detail::group_type::k_width;
		const std::size_t rehash_peak_bytes =
			stats.peak_allocated_bytes + rehashed_control_bytes;
		require(map.capacity() == original_capacity * 2);
		std::cout << "initial_capacity=" << original_capacity
				  << ", rehashed_capacity=" << map.capacity()
				  << ", value_type=" << value_type_bytes << " B"
				  << ", map=" << map_object_bytes << " B"
				  << ", slot=" << slot_bytes_per_bucket << " B"
				  << ", control=" << control_bytes << " B"
				  << ", initial_table=" << table_bytes << " B"
				  << ", rehash_peak=" << rehash_peak_bytes << " B... ";
	}

	require(stats.allocation_count == stats.deallocation_count);
	require(stats.allocated_bytes == stats.deallocated_bytes);
	require(stats.current_allocated_bytes == 0);
	std::cout << "PASSED!\n";
}

void test_type_specific_memory_footprints() {
	std::cout << "[Test 16] Type-specific memory footprints... " << std::flush;

	struct large_record {
		std::array<std::byte, 256> bytes;
	};

	const reserved_table_measurement string_key =
		measure_reserved_table<std::string, int>(100000);
	const reserved_table_measurement large_value =
		measure_reserved_table<int, large_record>(10000);
	require(string_key.slot_bytes_per_bucket > sizeof(std::pair<const int, int>));
	require(large_value.slot_bytes_per_bucket > string_key.slot_bytes_per_bucket);

	std::cout << "string_key: capacity=" << string_key.capacity
			  << ", slot=" << string_key.slot_bytes_per_bucket << " B"
			  << ", table=" << string_key.table_bytes << " B; large_value: capacity="
			  << large_value.capacity
			  << ", slot=" << large_value.slot_bytes_per_bucket << " B"
			  << ", table=" << large_value.table_bytes << " B... ";
	std::cout << "PASSED!\n";
}

void test_stateful_allocator_ownership() {
	std::cout << "[Test 17] Stateful allocator ownership and propagation... "
			  << std::flush;

	using pair_type = std::pair<const int, int>;
	using allocator_type = tracking_allocator<pair_type>;
	using map_type = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   allocator_type>;

	allocator_stats first_stats;
	allocator_stats second_stats;
	const allocator_type first_allocator(&first_stats);
	const allocator_type second_allocator(&second_stats);

	{
		map_type source(16, {}, {}, first_allocator);
		for (int i = 0; i < 100; ++i) {
			source.insert({i, i * 10});
		}

		map_type copied(source);
		require(copied.get_allocator() == first_allocator);
		require(copied.at(99) == 990);

		map_type copy_target(16, {}, {}, second_allocator);
		copy_target = source;
		require(copy_target.get_allocator() == second_allocator);
		require(copy_target.at(99) == 990);

		map_type move_target(16, {}, {}, second_allocator);
		move_target = std::move(copied);
		require(move_target.get_allocator() == second_allocator);
		require(move_target.at(99) == 990);
		require(copied.empty());

		bool did_throw = false;
		try {
			source.swap(move_target);
		} catch (const std::invalid_argument&) {
			did_throw = true;
		}
		require(did_throw);
		require(source.at(99) == 990);
		require(move_target.at(99) == 990);
	}

	require(first_stats.allocation_count == first_stats.deallocation_count);
	require(second_stats.allocation_count == second_stats.deallocation_count);

	std::cout << "PASSED!\n";
}

void test_propagating_allocator_ownership() {
	std::cout << "[Test 18] Propagating allocator ownership... " << std::flush;

	using pair_type = std::pair<const int, int>;
	using allocator_type = tracking_allocator<pair_type, true>;
	using map_type = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   allocator_type>;

	allocator_stats first_stats;
	allocator_stats second_stats;
	const allocator_type first_allocator(&first_stats);
	const allocator_type second_allocator(&second_stats);

	{
		map_type source(16, {}, {}, first_allocator);
		source.insert({1, 10});

		map_type copy_target(16, {}, {}, second_allocator);
		copy_target.insert({2, 20});
		copy_target = source;
		require(copy_target.get_allocator() == first_allocator);
		require(copy_target.at(1) == 10);

		map_type move_target(16, {}, {}, second_allocator);
		move_target = std::move(copy_target);
		require(move_target.get_allocator() == first_allocator);
		require(move_target.at(1) == 10);
		require(copy_target.empty());

		map_type swap_target(16, {}, {}, second_allocator);
		swap_target.insert({3, 30});
		move_target.swap(swap_target);
		require(move_target.get_allocator() == second_allocator);
		require(move_target.at(3) == 30);
		require(swap_target.get_allocator() == first_allocator);
		require(swap_target.at(1) == 10);
	}

	require(first_stats.allocation_count == first_stats.deallocation_count);
	require(second_stats.allocation_count == second_stats.deallocation_count);

	std::cout << "PASSED!\n";
}

void test_lifecycle_and_capacity_controls() {
	std::cout << "[Test 19] Lifecycle and capacity controls... " << std::flush;

	nova::hash_map<int, int> map;
	require(map.capacity() == 0);
	map.reserve(0);
	require(map.capacity() == 0);

	map.insert({1, 10});
	map.insert({2, 20});
	const std::size_t allocated_capacity = map.capacity();
	map.clear();
	require(map.empty());
	require(map.capacity() == allocated_capacity);

	map.try_emplace(3, 30);
	require(map.at(3) == 30);
	map.clear();
	map.rehash(0);
	require(map.capacity() == 0);

	nova::hash_map<int, int> other;
	map.insert({4, 40});
	other.insert({5, 50});
	map.swap(other);
	require(map.at(5) == 50);
	require(other.at(4) == 40);

	std::cout << "PASSED!\n";
}

void test_hash_exception_preserves_rehash_source() {
	std::cout << "[Test 20] Hash exception preserves rehash source... "
			<< std::flush;

	toggle_throw_hash::should_throw = false;
	nova::hash_map<int, int, toggle_throw_hash> map;
	map.insert({1, 10});
	map.insert({2, 20});
	const std::size_t original_capacity = map.capacity();

	toggle_throw_hash::should_throw = true;
	bool did_throw = false;
	try {
		map.rehash(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	toggle_throw_hash::should_throw = false;

	require(did_throw);
	require(map.capacity() == original_capacity);
	require(map.size() == 2);
	require(map.at(1) == 10);
	require(map.at(2) == 20);

	std::cout << "PASSED!\n";
}

void test_allocator_move_assignment_exception() {
	std::cout << "[Test 21] Allocator move-assignment exception... "
			<< std::flush;

	using pair_type = std::pair<const int, int>;
	using allocator_type = throwing_move_allocator<pair_type>;
	using map_type = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   allocator_type>;

	allocator_stats source_stats;
	allocator_stats target_stats;
	{
		map_type source(16, {}, {}, allocator_type(&source_stats));
		map_type target(16, {}, {}, allocator_type(&target_stats));
		source.insert({1, 10});
		target.insert({2, 20});

		allocator_type::throw_on_move_assignment = true;
		bool did_throw = false;
		try {
			target = std::move(source);
		} catch (const std::runtime_error&) {
			did_throw = true;
		}
		allocator_type::throw_on_move_assignment = false;

		require(did_throw);
		require(target.empty());
		require(source.at(1) == 10);
		require(target.try_emplace(3, 30).second);
		require(target.at(3) == 30);
	}

	require(source_stats.allocation_count == source_stats.deallocation_count);
	require(target_stats.allocation_count == target_stats.deallocation_count);
	std::cout << "PASSED!\n";
}

void test_long_string_payload_memory() {
	std::cout << "[Test 22] Long-string payload memory... " << std::flush;

	using pair_type = std::pair<const tracked_string, int>;
	using allocator_type = tracking_allocator<pair_type>;
	using map_type = nova::hash_map<tracked_string, int, tracked_string_hash,
		std::equal_to<tracked_string>, allocator_type>;

	constexpr int k_element_count = 10000;
	constexpr std::size_t k_string_length = 128;
	allocator_stats stats;
	{
		map_type map(0, {}, {}, allocator_type(&stats));
		map.reserve(k_element_count);
		const std::size_t slot_bytes = stats.current_allocated_bytes;

		for (int i = 0; i < k_element_count; ++i) {
			tracked_string key(k_string_length, 'x', tracking_allocator<char>(&stats));
			const std::string id = std::to_string(i);
			key.replace(0, id.size(), id.data(), id.size());
			require(map.try_emplace(std::move(key), i).second);
		}

		const std::size_t payload_bytes =
			stats.current_allocated_bytes - slot_bytes;
		const std::size_t control_bytes =
			map.capacity() + nova::containers::detail::group_type::k_width;
		const std::size_t total_bytes =
			stats.current_allocated_bytes + control_bytes;
		require(map.size() == k_element_count);
		require(payload_bytes >= k_element_count * (k_string_length + 1));
		std::cout << "capacity=" << map.capacity()
				  << ", slots=" << slot_bytes << " B"
				  << ", payload=" << payload_bytes << " B"
				  << ", control=" << control_bytes << " B"
				  << ", total=" << total_bytes << " B... ";
	}

	require(stats.allocation_count == stats.deallocation_count);
	require(stats.allocated_bytes == stats.deallocated_bytes);
	require(stats.current_allocated_bytes == 0);
	std::cout << "PASSED!\n";
}

void test_heterogeneous_lookup() {
	std::cout << "[Test 23] Heterogeneous string lookup... " << std::flush;

	using map_type = nova::hash_map<std::string, int, transparent_string_hash,
		transparent_string_equal>;
	map_type map;
	map.try_emplace("alpha", 10);
	map.try_emplace("beta", 20);

	const std::string_view alpha = "alpha";
	const std::string_view missing = "missing";
	require(map.find(alpha) != map.end());
	require(map.find(alpha)->second == 10);
	require(map.contains(alpha));
	require(map.count(alpha) == 1);
	require(!map.contains(missing));

	const map_type& const_map = map;
	require(const_map.find(std::string_view("beta")) != const_map.end());
	require(map.erase(alpha));
	require(!map.contains(alpha));

	std::cout << "PASSED!\n";
}

void test_single_probe_insertion() {
	std::cout << "[Test 24] Single-probe insertion... " << std::flush;

	nova::hash_map<int, int, counting_hash> map;
	map.reserve(8);

	counting_hash::calls = 0;
	const auto [first, first_inserted] = map.try_emplace(1, 10);
	require(first_inserted);
	require(first->second == 10);
	require(counting_hash::calls == 1);

	counting_hash::calls = 0;
	const auto [duplicate, duplicate_inserted] = map.try_emplace(1, 20);
	require(!duplicate_inserted);
	require(duplicate->second == 10);
	require(counting_hash::calls == 1);

	counting_hash::calls = 0;
	const auto [assigned, assigned_inserted] = map.insert_or_assign(2, 30);
	require(assigned_inserted);
	require(assigned->second == 30);
	require(counting_hash::calls == 1);

	std::cout << "PASSED!\n";
}

void test_deferred_construction_exception_safety() {
	std::cout << "[Test 25] Deferred construction exception safety... "
			  << std::flush;

	nova::hash_map<int, throw_on_construction> map;
	map.reserve(8);
	map.try_emplace(1, 10);

	throw_on_construction::should_throw = true;
	const auto [duplicate, duplicate_inserted] = map.try_emplace(1, 20);
	require(!duplicate_inserted);
	require(duplicate->second.value == 10);

	bool threw = false;
	try {
		map.try_emplace(2, 20);
	} catch (const std::runtime_error&) {
		threw = true;
	}
	require(threw);
	require(map.size() == 1);
	require(!map.contains(2));

	throw_on_construction::should_throw = false;
	const auto [inserted, was_inserted] = map.try_emplace(2, 20);
	require(was_inserted);
	require(inserted->second.value == 20);

	std::cout << "PASSED!\n";
}

int main() {
  std::cout << "=== Running NovaContainers Tests ===\n\n";

  test_basic_operations();
  test_large_scale_rehash();
  test_iteration();
	test_const_lookup_and_bounds_checking();
	test_hash_exceptions_propagate();
	test_equality_exception_preserves_rehash_source();
	test_stateful_hash_survives_copy_and_move();
	test_rehash_preserves_table_when_copy_throws();
	test_move_only_rehash_preserves_structural_invariants();
	test_tombstones_are_compacted_without_growth();
	test_control_tail_wrap_around();
	test_extended_container_api();
	test_differential_operations();
	test_collision_stress_operations();
	test_memory_footprint_accounting();
	test_type_specific_memory_footprints();
	test_stateful_allocator_ownership();
	test_propagating_allocator_ownership();
	test_lifecycle_and_capacity_controls();
	test_hash_exception_preserves_rehash_source();
	test_allocator_move_assignment_exception();
	test_long_string_payload_memory();
	test_heterogeneous_lookup();
	test_single_probe_insertion();
	test_deferred_construction_exception_safety();
  std::cout << "\nAll NovaContainers tests completed successfully!\n";
  return 0;
}
