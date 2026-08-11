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

#include "NovaContainers/NovaContainers.hpp"

void Require(bool condition) {
	if (!condition) {
		throw std::runtime_error("test requirement failed");
	}
}

void RequireDifferential(bool condition, uint32_t seed, int operation,
					 const char* check) {
	if (!condition) {
		throw std::runtime_error(
			"differential failure: seed=" + std::to_string(seed) +
			", operation=" + std::to_string(operation) + ", check=" + check);
	}
}

void TestBasicOperations() {
  std::cout << "[Test 1] Basic Operations (Insert, Find, Erase, operator[])... " << std::flush;

  nova::hash_map<std::string, int> map;

  Require(map.empty());
  Require(map.size() == 0);

  // Insert test
  auto [it1, inserted1] = map.insert({"apple", 100});
  Require(inserted1);
  Require(it1->first == "apple");
  Require(it1->second == 100);
  Require(map.size() == 1);

  // Duplicate insert test
  auto [it2, inserted2] = map.insert({"apple", 200});
  Require(!inserted2);
  Require(it2->second == 100);
  Require(map.size() == 1);

  // operator[] test
  map["banana"] = 200;
  map["cherry"] = 300;
  Require(map.size() == 3);
  Require(map["apple"] == 100);
  Require(map["banana"] == 200);
  Require(map["cherry"] == 300);

  // Find test
  auto find_it = map.find("banana");
  Require(find_it != map.end());
  Require(find_it->second == 200);

  auto not_found_it = map.find("durian");
  Require(not_found_it == map.end());

  // Erase test
  bool erased = map.erase("banana");
  Require(erased);
  Require(map.size() == 2);
  Require(map.find("banana") == map.end());

  bool erase_again = map.erase("banana");
  Require(!erase_again);

  std::cout << "PASSED!\n";
}

void TestLargeScaleRehash() {
  std::cout << "[Test 2] Large-scale Insert & Rehash (100,000 items)... " << std::flush;

  constexpr int kCount = 100000;
  nova::hash_map<int, int> map;

  for (int i = 0; i < kCount; ++i) {
    map[i] = i * 2;
  }

  Require(map.size() == kCount);

  for (int i = 0; i < kCount; ++i) {
    auto it = map.find(i);
    Require(it != map.end());
    Require(it->second == i * 2);
  }

  // Erase half
  for (int i = 0; i < kCount; i += 2) {
    bool erased = map.erase(i);
    Require(erased);
  }

  Require(map.size() == kCount / 2);

  for (int i = 0; i < kCount; ++i) {
    auto it = map.find(i);
    if (i % 2 == 0) {
      Require(it == map.end());
    } else {
      Require(it != map.end());
      Require(it->second == i * 2);
    }
  }

  std::cout << "PASSED!\n";
}

void TestIteration() {
  std::cout << "[Test 3] Iteration & Range-based For Loop... " << std::flush;

  nova::hash_map<std::string, int> map = {
      {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}};

  int sum = 0;
  int count = 0;
  for (const auto& [key, value] : map) {
    sum += value;
    ++count;
  }

  Require(count == 4);
  Require(sum == 10);

  std::cout << "PASSED!\n";
}

struct ThrowingHash {
	std::size_t operator()(int key) const {
		if (key == 42) {
			throw std::runtime_error("hash failure");
		}
		return std::hash<int>{}(key);
	}
};

struct ToggleThrowHash {
	std::size_t operator()(int key) const {
		if (should_throw) {
			throw std::runtime_error("rehash hash failure");
		}
		return std::hash<int>{}(key);
	}

	inline static bool should_throw = false;
};

struct CollisionHash {
	std::size_t operator()(int) const noexcept { return 0; }
};

struct CountingHash {
	std::size_t operator()(int key) const noexcept {
		++calls;
		return std::hash<int>{}(key);
	}

	inline static std::size_t calls = 0;
};

struct ThrowOnConstruction {
	explicit ThrowOnConstruction(int initial_value) : value(initial_value) {
		if (should_throw) {
			throw std::runtime_error("construction failure");
		}
	}

	inline static bool should_throw = false;
	int value;
};

struct ThrowingEquality {
	bool operator()(int left, int right) const {
		if (should_throw) {
			throw std::runtime_error("equality failure");
		}
		return left == right;
	}

	inline static bool should_throw = false;
};

struct SeededHash {
	SeededHash() : seed(next_seed++ * 0x9E3779B97F4A7C15ULL) {}

	std::size_t operator()(int key) const noexcept {
		return std::hash<int>{}(key) ^ seed;
	}

	inline static std::size_t next_seed = 1;
	std::size_t seed;
};

struct ThrowOnCopy {
	explicit ThrowOnCopy(int initial_value) : value(initial_value) {}

	ThrowOnCopy(const ThrowOnCopy& other) : value(other.value) {
		if (throw_on_copy) {
			throw std::runtime_error("copy failure");
		}
	}

	ThrowOnCopy(ThrowOnCopy&&) noexcept = default;
	ThrowOnCopy& operator=(const ThrowOnCopy&) = default;
	ThrowOnCopy& operator=(ThrowOnCopy&&) noexcept = default;

	inline static bool throw_on_copy = false;
	int value;
};

struct ThrowOnMove {
	explicit ThrowOnMove(int initial_value) : value(initial_value) {}

	ThrowOnMove(const ThrowOnMove&) = delete;
	ThrowOnMove& operator=(const ThrowOnMove&) = delete;

	ThrowOnMove(ThrowOnMove&& other) : value(other.value) {
		if (moves_before_throw == 0) {
			throw std::runtime_error("move failure");
		}
		if (moves_before_throw > 0) {
			--moves_before_throw;
		}
		other.value = -1;
	}

	ThrowOnMove& operator=(ThrowOnMove&&) = delete;

	inline static int moves_before_throw = -1;
	int value;
};

struct TailProbeHash {
	std::size_t operator()(int) const noexcept {
		return nova::containers::detail::Group::kWidth - 1;
	}
};

struct ConstructionCounter {
	explicit ConstructionCounter(int initial_value) : value(initial_value) {
		++construction_count;
	}

	ConstructionCounter(const ConstructionCounter& other) : value(other.value) {
		++construction_count;
	}
	ConstructionCounter(ConstructionCounter&& other) noexcept : value(other.value) {
		++construction_count;
	}
	ConstructionCounter& operator=(const ConstructionCounter&) = default;
	ConstructionCounter& operator=(ConstructionCounter&&) noexcept = default;

	inline static int construction_count = 0;
	int value;
};

struct AllocatorStats {
	std::size_t allocation_count = 0;
	std::size_t deallocation_count = 0;
	std::size_t allocated_bytes = 0;
	std::size_t deallocated_bytes = 0;
	std::size_t current_allocated_bytes = 0;
	std::size_t peak_allocated_bytes = 0;
};

template <typename T, bool kPropagate = false>
class TrackingAllocator {
 public:
	using value_type = T;
	using propagate_on_container_copy_assignment =
		std::bool_constant<kPropagate>;
	using propagate_on_container_move_assignment =
		std::bool_constant<kPropagate>;
	using propagate_on_container_swap = std::bool_constant<kPropagate>;
	using is_always_equal = std::false_type;

	template <typename U>
	struct rebind {
		using other = TrackingAllocator<U, kPropagate>;
	};

	TrackingAllocator() noexcept = default;
	explicit TrackingAllocator(AllocatorStats* stats) noexcept : stats_(stats) {}

	template <typename U>
	TrackingAllocator(const TrackingAllocator<U, kPropagate>& other) noexcept
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

	[[nodiscard]] AllocatorStats* stats() const noexcept { return stats_; }

	template <typename U>
	[[nodiscard]] bool operator==(
		const TrackingAllocator<U, kPropagate>& other) const noexcept {
		return stats_ == other.stats();
	}

 private:
	AllocatorStats* stats_ = nullptr;
};

template <typename T>
class ThrowingMoveAllocator {
 public:
	using value_type = T;
	using propagate_on_container_move_assignment = std::true_type;
	using is_always_equal = std::false_type;

	template <typename U>
	struct rebind {
		using other = ThrowingMoveAllocator<U>;
	};

	ThrowingMoveAllocator() noexcept = default;
	explicit ThrowingMoveAllocator(AllocatorStats* stats) noexcept : stats_(stats) {}
	ThrowingMoveAllocator(const ThrowingMoveAllocator&) noexcept = default;
	ThrowingMoveAllocator(ThrowingMoveAllocator&&) noexcept = default;

	template <typename U>
	ThrowingMoveAllocator(const ThrowingMoveAllocator<U>& other) noexcept
		: stats_(other.stats()) {}

	ThrowingMoveAllocator& operator=(const ThrowingMoveAllocator&) noexcept = default;
	ThrowingMoveAllocator& operator=(ThrowingMoveAllocator&& other) {
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

	[[nodiscard]] AllocatorStats* stats() const noexcept { return stats_; }

	template <typename U>
	[[nodiscard]] bool operator==(
		const ThrowingMoveAllocator<U>& other) const noexcept {
		return stats_ == other.stats();
	}

	inline static bool throw_on_move_assignment = false;

 private:
	AllocatorStats* stats_ = nullptr;
};

struct ReservedTableMeasurement {
	std::size_t capacity = 0;
	std::size_t slot_bytes_per_bucket = 0;
	std::size_t control_bytes = 0;
	std::size_t table_bytes = 0;
};

template <typename Key, typename Value>
ReservedTableMeasurement MeasureReservedTable(std::size_t element_count) {
	using Pair = std::pair<const Key, Value>;
	using Allocator = TrackingAllocator<Pair>;
	using Map = nova::hash_map<Key, Value, std::hash<Key>, std::equal_to<Key>,
						   Allocator>;

	AllocatorStats stats;
	ReservedTableMeasurement measurement;
	{
		Map map(0, {}, {}, Allocator(&stats));
		map.reserve(element_count);
		Require(map.capacity() >= element_count);
		Require(stats.allocation_count == 1);
		Require(stats.allocated_bytes % map.capacity() == 0);

		measurement.capacity = map.capacity();
		measurement.slot_bytes_per_bucket =
			stats.allocated_bytes / map.capacity();
		measurement.control_bytes =
			map.capacity() + nova::containers::detail::Group::kWidth;
		measurement.table_bytes = stats.allocated_bytes + measurement.control_bytes;
	}

	Require(stats.allocation_count == stats.deallocation_count);
	Require(stats.allocated_bytes == stats.deallocated_bytes);
	Require(stats.current_allocated_bytes == 0);
	return measurement;
}

using TrackedString =
	std::basic_string<char, std::char_traits<char>, TrackingAllocator<char>>;

struct TrackedStringHash {
	std::size_t operator()(const TrackedString& value) const noexcept {
		return std::hash<std::string_view>{}(
			std::string_view(value.data(), value.size()));
	}
};

struct TransparentStringHash {
	using is_transparent = void;

	std::size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}
};

struct TransparentStringEqual {
	using is_transparent = void;

	bool operator()(std::string_view left, std::string_view right) const noexcept {
		return left == right;
	}
};

void TestConstLookupAndBoundsChecking() {
	std::cout << "[Test 4] Const lookup, contains, and at... " << std::flush;

	const nova::hash_map<std::string, int> map = {{"answer", 42}};
	const auto iterator = map.find("answer");
	Require(iterator != map.end());
	Require(iterator->second == 42);
	Require(map.contains("answer"));
	Require(!map.contains("missing"));
	Require(map.at("answer") == 42);

	bool did_throw = false;
	try {
		static_cast<void>(map.at("missing"));
	} catch (const std::out_of_range&) {
		did_throw = true;
	}
	Require(did_throw);

	std::cout << "PASSED!\n";
}

void TestHashExceptionsPropagate() {
	std::cout << "[Test 5] Hash exceptions propagate... " << std::flush;

	nova::hash_map<int, int, ThrowingHash> map;
	map.insert({1, 10});
	bool did_throw = false;
	try {
		static_cast<void>(map.find(42));
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	Require(did_throw);

	std::cout << "PASSED!\n";
}

void TestEqualityExceptionPreservesRehashSource() {
	std::cout << "[Test 6] Equality exception preserves rehash source... "
			<< std::flush;

	nova::hash_map<int, int, CollisionHash, ThrowingEquality> map;
	map.insert({1, 10});
	map.insert({2, 20});
	const std::size_t original_capacity = map.capacity();

	ThrowingEquality::should_throw = true;
	bool did_throw = false;
	try {
		map.rehash(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	ThrowingEquality::should_throw = false;

	Require(did_throw);
	Require(map.capacity() == original_capacity);
	Require(map.size() == 2);
	Require(map.at(1) == 10);
	Require(map.at(2) == 20);

	std::cout << "PASSED!\n";
}

void TestStatefulHashSurvivesCopyAndMove() {
	std::cout << "[Test 7] Stateful hash survives copy and move... "
			  << std::flush;

	nova::hash_map<int, int, SeededHash> source;
	source.insert({7, 70});
	source.insert({11, 110});

	nova::hash_map<int, int, SeededHash> copied(source);
	Require(copied.at(7) == 70);
	Require(copied.at(11) == 110);

	nova::hash_map<int, int, SeededHash> moved(std::move(source));
	Require(moved.at(7) == 70);
	Require(moved.at(11) == 110);

	std::cout << "PASSED!\n";
}

void TestRehashPreservesTableWhenCopyThrows() {
	std::cout << "[Test 8] Failed rehash preserves the original table... "
			  << std::flush;

	nova::hash_map<int, ThrowOnCopy> map;
	map.emplace(1, ThrowOnCopy(10));
	map.emplace(2, ThrowOnCopy(20));
	const std::size_t original_capacity = map.capacity();

	ThrowOnCopy::throw_on_copy = true;
	bool did_throw = false;
	try {
		map.reserve(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	ThrowOnCopy::throw_on_copy = false;

	Require(did_throw);
	Require(map.capacity() == original_capacity);
	Require(map.size() == 2);
	Require(map.at(1).value == 10);
	Require(map.at(2).value == 20);

	std::cout << "PASSED!\n";
}

void TestMoveOnlyRehashPreservesStructuralInvariants() {
	std::cout << "[Test 9] Move-only rehash keeps structural invariants... "
			<< std::flush;

	ThrowOnMove::moves_before_throw = -1;
	nova::hash_map<int, ThrowOnMove> map;
	map.try_emplace(1, 10);
	map.try_emplace(2, 20);
	const std::size_t original_capacity = map.capacity();

	ThrowOnMove::moves_before_throw = 1;
	bool did_throw = false;
	try {
		map.rehash(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	ThrowOnMove::moves_before_throw = -1;

	Require(did_throw);
	Require(map.capacity() == original_capacity);
	Require(map.size() == 2);
	Require(map.find(1) != map.end());
	Require(map.find(2) != map.end());
	Require(map.try_emplace(3, 30).second);
	Require(map.at(3).value == 30);

	std::cout << "PASSED!\n";
}

void TestTombstonesAreCompactedWithoutGrowth() {
	std::cout << "[Test 10] Tombstones compact without capacity growth... "
			  << std::flush;

	nova::hash_map<int, int> map;
	constexpr int kGrowthLimit =
		static_cast<int>(nova::containers::detail::Group::kWidth * 7 / 8);
	constexpr int kEraseCount = kGrowthLimit / 2;
	for (int i = 0; i < kGrowthLimit; ++i) {
		map.insert({i, i * 10});
	}
	Require(map.capacity() == nova::containers::detail::Group::kWidth);

	for (int i = 0; i < kEraseCount; ++i) {
		Require(map.erase(i));
	}
	map.insert({100, 1000});

	Require(map.capacity() == nova::containers::detail::Group::kWidth);
	Require(map.size() == kGrowthLimit - kEraseCount + 1);
	for (int i = kEraseCount; i < kGrowthLimit; ++i) {
		Require(map.at(i) == i * 10);
	}
	Require(map.at(100) == 1000);

	std::cout << "PASSED!\n";
}

void TestControlTailWrapAround() {
	std::cout << "[Test 11] Control tail wrap-around probing... " << std::flush;

	nova::hash_map<int, int, TailProbeHash> map;
	constexpr int kGrowthLimit =
		static_cast<int>(nova::containers::detail::Group::kWidth * 7 / 8);
	for (int i = 0; i < kGrowthLimit; ++i) {
		Require(map.insert({i, i * 10}).second);
	}
	Require(map.capacity() == nova::containers::detail::Group::kWidth);

	Require(map.erase(1));
	Require(map.find(1) == map.end());
	for (int i = 2; i < kGrowthLimit; ++i) {
		Require(map.at(i) == i * 10);
	}

	Require(map.insert({100, 1000}).second);
	Require(map.at(100) == 1000);
	for (int i = 2; i < kGrowthLimit; ++i) {
		Require(map.at(i) == i * 10);
	}

	std::cout << "PASSED!\n";
}

void TestExtendedContainerApi() {
	std::cout << "[Test 12] Extended container API... " << std::flush;

	ConstructionCounter::construction_count = 0;
	nova::hash_map<int, ConstructionCounter> map;
	auto [first, first_inserted] = map.try_emplace(7, 70);
	Require(first_inserted);
	Require(first->second.value == 70);
	const int construction_count_after_first =
		ConstructionCounter::construction_count;
	Require(construction_count_after_first > 0);

	auto [duplicate, duplicate_inserted] = map.try_emplace(7, 700);
	Require(!duplicate_inserted);
	Require(duplicate->second.value == 70);
	Require(ConstructionCounter::construction_count == construction_count_after_first);

	auto [assigned, assigned_inserted] =
		map.insert_or_assign(7, ConstructionCounter(71));
	Require(!assigned_inserted);
	Require(assigned->second.value == 71);
	Require(map.count(7) == 1);
	Require(map.count(8) == 0);
	Require(map.load_factor() > 0.0F);
	Require(map.max_load_factor() == 0.875F);

	map.try_emplace(8, 80);
	const nova::hash_map<int, ConstructionCounter>::const_iterator const_it =
		map.begin();
	const int erased_key = const_it->first;
	auto next = map.erase(const_it);
	Require(next == map.end() || next->first != erased_key);
	Require(map.size() == 1);

	map.reserve(15);
	Require(map.capacity() >= 32);
	map.rehash(16);
	Require(map.capacity() >= nova::containers::detail::Group::kWidth);

	std::cout << "PASSED!\n";
}

void TestDifferentialOperations() {
	std::cout << "[Test 13] Differential operations against std::unordered_map... "
			<< std::flush;

	constexpr int kOperationCount = 100000;
	constexpr int kKeyRange = 257;
	uint32_t state = 0xC0FFEEU;
	auto next_random = [&state]() {
		state = state * 1664525U + 1013904223U;
		return state;
	};

	nova::hash_map<int, int> nova_map;
	std::unordered_map<int, int> std_map;
	for (int operation = 0; operation < kOperationCount; ++operation) {
		const int key = static_cast<int>(next_random() % kKeyRange);
		const int value = static_cast<int>(next_random());
		switch (next_random() % 4U) {
			case 0: {
				auto [nova_it, nova_inserted] = nova_map.try_emplace(key, value);
				auto [std_it, std_inserted] = std_map.try_emplace(key, value);
				Require(nova_inserted == std_inserted);
				Require(nova_it->second == std_it->second);
				break;
			}
			case 1: {
				auto [nova_it, nova_inserted] =
					nova_map.insert_or_assign(key, value);
				auto [std_it, std_inserted] =
					std_map.insert_or_assign(key, value);
				Require(nova_inserted == std_inserted);
				Require(nova_it->second == std_it->second);
				break;
			}
			case 2:
				Require(nova_map.erase(key) == (std_map.erase(key) != 0));
				break;
			default: {
				const auto nova_it = nova_map.find(key);
				const auto std_it = std_map.find(key);
				Require((nova_it == nova_map.end()) == (std_it == std_map.end()));
				if (nova_it != nova_map.end()) {
					Require(nova_it->second == std_it->second);
				}
				break;
			}
		}

		Require(nova_map.size() == std_map.size());
	}

	for (const auto& [key, value] : std_map) {
		const auto nova_it = nova_map.find(key);
		Require(nova_it != nova_map.end());
		Require(nova_it->second == value);
	}

	std::cout << "PASSED!\n";
}

void TestCollisionStressOperations() {
	std::cout << "[Test 14] Collision stress against std::unordered_map... "
			<< std::flush;

	using NovaMap = nova::hash_map<int, int, CollisionHash>;
	using StdMap = std::unordered_map<int, int, CollisionHash>;
	constexpr uint32_t kSeeds[] = {0x12345678U, 0x9ABCDEF0U, 0xCAFEBABEU};
	constexpr int kOperationCount = 50000;
	constexpr int kKeyRange = 64;

	for (const uint32_t seed : kSeeds) {
		uint32_t state = seed;
		auto next_random = [&state]() {
			state = state * 1664525U + 1013904223U;
			return state;
		};

		NovaMap nova_map;
		StdMap std_map;
		for (int operation = 0; operation < kOperationCount; ++operation) {
			const int key = static_cast<int>(next_random() % kKeyRange);
			const int value = static_cast<int>(next_random());
			switch (next_random() % 7U) {
				case 0: {
					auto [nova_it, nova_inserted] =
						nova_map.insert_or_assign(key, value);
					auto [std_it, std_inserted] =
						std_map.insert_or_assign(key, value);
					RequireDifferential(nova_inserted == std_inserted, seed,
						operation, "insert_or_assign result");
					RequireDifferential(nova_it->second == std_it->second, seed,
						operation, "insert_or_assign value");
					break;
				}
				case 1: {
					auto [nova_it, nova_inserted] =
						nova_map.try_emplace(key, value);
					auto [std_it, std_inserted] =
						std_map.try_emplace(key, value);
					RequireDifferential(nova_inserted == std_inserted, seed,
						operation, "try_emplace result");
					RequireDifferential(nova_it->second == std_it->second, seed,
						operation, "try_emplace value");
					break;
				}
				case 2:
					RequireDifferential(nova_map.erase(key) == (std_map.erase(key) != 0),
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
					NovaMap copied(nova_map);
					NovaMap moved(std::move(copied));
					NovaMap reassigned;
					reassigned = std::move(moved);
					nova_map = reassigned;
					break;
				}
				default: {
					const auto nova_it = nova_map.find(key);
					const auto std_it = std_map.find(key);
					RequireDifferential(
						(nova_it == nova_map.end()) == (std_it == std_map.end()), seed,
						operation, "find presence");
					if (nova_it != nova_map.end()) {
						RequireDifferential(nova_it->second == std_it->second, seed,
							operation, "find value");
					}
					break;
				}
			}

			RequireDifferential(nova_map.size() == std_map.size(), seed, operation,
				"size");
			if (operation % 127 == 0) {
				for (const auto& [std_key, std_value] : std_map) {
					const auto nova_it = nova_map.find(std_key);
					RequireDifferential(nova_it != nova_map.end(), seed, operation,
						"full comparison presence");
					RequireDifferential(nova_it->second == std_value, seed, operation,
						"full comparison value");
				}
			}
		}
	}

	std::cout << "PASSED!\n";
}

void TestMemoryFootprintAccounting() {
	std::cout << "[Test 15] Memory footprint accounting... " << std::flush;

	using Pair = std::pair<const int, int>;
	using Allocator = TrackingAllocator<Pair>;
	using Map = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   Allocator>;

	constexpr std::size_t kElementCount = 100000;
	AllocatorStats stats;
	{
		Map map(0, {}, {}, Allocator(&stats));
		Require(map.capacity() == 0);
		Require(stats.allocation_count == 0);

		map.reserve(kElementCount);
		Require(map.capacity() >= kElementCount);
		Require(stats.allocation_count == 1);
		Require(stats.allocated_bytes % map.capacity() == 0);

		const std::size_t slot_bytes_per_bucket =
			stats.allocated_bytes / map.capacity();
		const std::size_t control_bytes =
			map.capacity() + nova::containers::detail::Group::kWidth;
		const std::size_t table_bytes = stats.allocated_bytes + control_bytes;
		const std::size_t original_capacity = map.capacity();
		const std::size_t value_type_bytes = sizeof(Pair);
		const std::size_t map_object_bytes = sizeof(Map);
		map.rehash(original_capacity * 2);
		const std::size_t rehashed_control_bytes =
			original_capacity + nova::containers::detail::Group::kWidth +
			map.capacity() + nova::containers::detail::Group::kWidth;
		const std::size_t rehash_peak_bytes =
			stats.peak_allocated_bytes + rehashed_control_bytes;
		Require(map.capacity() == original_capacity * 2);
		std::cout << "initial_capacity=" << original_capacity
				  << ", rehashed_capacity=" << map.capacity()
				  << ", value_type=" << value_type_bytes << " B"
				  << ", map=" << map_object_bytes << " B"
				  << ", slot=" << slot_bytes_per_bucket << " B"
				  << ", control=" << control_bytes << " B"
				  << ", initial_table=" << table_bytes << " B"
				  << ", rehash_peak=" << rehash_peak_bytes << " B... ";
	}

	Require(stats.allocation_count == stats.deallocation_count);
	Require(stats.allocated_bytes == stats.deallocated_bytes);
	Require(stats.current_allocated_bytes == 0);
	std::cout << "PASSED!\n";
}

void TestTypeSpecificMemoryFootprints() {
	std::cout << "[Test 16] Type-specific memory footprints... " << std::flush;

	struct LargeValue {
		std::array<std::byte, 256> bytes;
	};

	const ReservedTableMeasurement string_key =
		MeasureReservedTable<std::string, int>(100000);
	const ReservedTableMeasurement large_value =
		MeasureReservedTable<int, LargeValue>(10000);
	Require(string_key.slot_bytes_per_bucket > sizeof(std::pair<const int, int>));
	Require(large_value.slot_bytes_per_bucket > string_key.slot_bytes_per_bucket);

	std::cout << "string_key: capacity=" << string_key.capacity
			  << ", slot=" << string_key.slot_bytes_per_bucket << " B"
			  << ", table=" << string_key.table_bytes << " B; large_value: capacity="
			  << large_value.capacity
			  << ", slot=" << large_value.slot_bytes_per_bucket << " B"
			  << ", table=" << large_value.table_bytes << " B... ";
	std::cout << "PASSED!\n";
}

void TestStatefulAllocatorOwnership() {
	std::cout << "[Test 17] Stateful allocator ownership and propagation... "
			  << std::flush;

	using Pair = std::pair<const int, int>;
	using Allocator = TrackingAllocator<Pair>;
	using Map = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   Allocator>;

	AllocatorStats first_stats;
	AllocatorStats second_stats;
	const Allocator first_allocator(&first_stats);
	const Allocator second_allocator(&second_stats);

	{
		Map source(16, {}, {}, first_allocator);
		for (int i = 0; i < 100; ++i) {
			source.insert({i, i * 10});
		}

		Map copied(source);
		Require(copied.get_allocator() == first_allocator);
		Require(copied.at(99) == 990);

		Map copy_target(16, {}, {}, second_allocator);
		copy_target = source;
		Require(copy_target.get_allocator() == second_allocator);
		Require(copy_target.at(99) == 990);

		Map move_target(16, {}, {}, second_allocator);
		move_target = std::move(copied);
		Require(move_target.get_allocator() == second_allocator);
		Require(move_target.at(99) == 990);
		Require(copied.empty());

		bool did_throw = false;
		try {
			source.Swap(move_target);
		} catch (const std::invalid_argument&) {
			did_throw = true;
		}
		Require(did_throw);
		Require(source.at(99) == 990);
		Require(move_target.at(99) == 990);
	}

	Require(first_stats.allocation_count == first_stats.deallocation_count);
	Require(second_stats.allocation_count == second_stats.deallocation_count);

	std::cout << "PASSED!\n";
}

void TestPropagatingAllocatorOwnership() {
	std::cout << "[Test 18] Propagating allocator ownership... " << std::flush;

	using Pair = std::pair<const int, int>;
	using Allocator = TrackingAllocator<Pair, true>;
	using Map = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   Allocator>;

	AllocatorStats first_stats;
	AllocatorStats second_stats;
	const Allocator first_allocator(&first_stats);
	const Allocator second_allocator(&second_stats);

	{
		Map source(16, {}, {}, first_allocator);
		source.insert({1, 10});

		Map copy_target(16, {}, {}, second_allocator);
		copy_target.insert({2, 20});
		copy_target = source;
		Require(copy_target.get_allocator() == first_allocator);
		Require(copy_target.at(1) == 10);

		Map move_target(16, {}, {}, second_allocator);
		move_target = std::move(copy_target);
		Require(move_target.get_allocator() == first_allocator);
		Require(move_target.at(1) == 10);
		Require(copy_target.empty());

		Map swap_target(16, {}, {}, second_allocator);
		swap_target.insert({3, 30});
		move_target.Swap(swap_target);
		Require(move_target.get_allocator() == second_allocator);
		Require(move_target.at(3) == 30);
		Require(swap_target.get_allocator() == first_allocator);
		Require(swap_target.at(1) == 10);
	}

	Require(first_stats.allocation_count == first_stats.deallocation_count);
	Require(second_stats.allocation_count == second_stats.deallocation_count);

	std::cout << "PASSED!\n";
}

void TestLifecycleAndCapacityControls() {
	std::cout << "[Test 19] Lifecycle and capacity controls... " << std::flush;

	nova::HashMap<int, int> map;
	Require(map.Capacity() == 0);
	map.Reserve(0);
	Require(map.Capacity() == 0);

	map.Insert({1, 10});
	map.Insert({2, 20});
	const std::size_t allocated_capacity = map.Capacity();
	map.Clear();
	Require(map.Empty());
	Require(map.Capacity() == allocated_capacity);

	map.TryEmplace(3, 30);
	Require(map.At(3) == 30);
	map.Clear();
	map.Rehash(0);
	Require(map.Capacity() == 0);

	nova::HashMap<int, int> other;
	map.Insert({4, 40});
	other.Insert({5, 50});
	map.Swap(other);
	Require(map.At(5) == 50);
	Require(other.At(4) == 40);

	std::cout << "PASSED!\n";
}

void TestHashExceptionPreservesRehashSource() {
	std::cout << "[Test 20] Hash exception preserves rehash source... "
			<< std::flush;

	ToggleThrowHash::should_throw = false;
	nova::hash_map<int, int, ToggleThrowHash> map;
	map.insert({1, 10});
	map.insert({2, 20});
	const std::size_t original_capacity = map.capacity();

	ToggleThrowHash::should_throw = true;
	bool did_throw = false;
	try {
		map.rehash(64);
	} catch (const std::runtime_error&) {
		did_throw = true;
	}
	ToggleThrowHash::should_throw = false;

	Require(did_throw);
	Require(map.capacity() == original_capacity);
	Require(map.size() == 2);
	Require(map.at(1) == 10);
	Require(map.at(2) == 20);

	std::cout << "PASSED!\n";
}

void TestAllocatorMoveAssignmentException() {
	std::cout << "[Test 21] Allocator move-assignment exception... "
			<< std::flush;

	using Pair = std::pair<const int, int>;
	using Allocator = ThrowingMoveAllocator<Pair>;
	using Map = nova::hash_map<int, int, std::hash<int>, std::equal_to<int>,
						   Allocator>;

	AllocatorStats source_stats;
	AllocatorStats target_stats;
	{
		Map source(16, {}, {}, Allocator(&source_stats));
		Map target(16, {}, {}, Allocator(&target_stats));
		source.insert({1, 10});
		target.insert({2, 20});

		Allocator::throw_on_move_assignment = true;
		bool did_throw = false;
		try {
			target = std::move(source);
		} catch (const std::runtime_error&) {
			did_throw = true;
		}
		Allocator::throw_on_move_assignment = false;

		Require(did_throw);
		Require(target.empty());
		Require(source.at(1) == 10);
		Require(target.try_emplace(3, 30).second);
		Require(target.at(3) == 30);
	}

	Require(source_stats.allocation_count == source_stats.deallocation_count);
	Require(target_stats.allocation_count == target_stats.deallocation_count);
	std::cout << "PASSED!\n";
}

void TestLongStringPayloadMemory() {
	std::cout << "[Test 22] Long-string payload memory... " << std::flush;

	using Pair = std::pair<const TrackedString, int>;
	using Allocator = TrackingAllocator<Pair>;
	using Map = nova::hash_map<TrackedString, int, TrackedStringHash,
		std::equal_to<TrackedString>, Allocator>;

	constexpr int kElementCount = 10000;
	constexpr std::size_t kStringLength = 128;
	AllocatorStats stats;
	{
		Map map(0, {}, {}, Allocator(&stats));
		map.reserve(kElementCount);
		const std::size_t slot_bytes = stats.current_allocated_bytes;

		for (int i = 0; i < kElementCount; ++i) {
			TrackedString key(kStringLength, 'x', TrackingAllocator<char>(&stats));
			const std::string id = std::to_string(i);
			key.replace(0, id.size(), id.data(), id.size());
			Require(map.try_emplace(std::move(key), i).second);
		}

		const std::size_t payload_bytes =
			stats.current_allocated_bytes - slot_bytes;
		const std::size_t control_bytes =
			map.capacity() + nova::containers::detail::Group::kWidth;
		const std::size_t total_bytes =
			stats.current_allocated_bytes + control_bytes;
		Require(map.size() == kElementCount);
		Require(payload_bytes >= kElementCount * (kStringLength + 1));
		std::cout << "capacity=" << map.capacity()
				  << ", slots=" << slot_bytes << " B"
				  << ", payload=" << payload_bytes << " B"
				  << ", control=" << control_bytes << " B"
				  << ", total=" << total_bytes << " B... ";
	}

	Require(stats.allocation_count == stats.deallocation_count);
	Require(stats.allocated_bytes == stats.deallocated_bytes);
	Require(stats.current_allocated_bytes == 0);
	std::cout << "PASSED!\n";
}

void TestHeterogeneousLookup() {
	std::cout << "[Test 23] Heterogeneous string lookup... " << std::flush;

	using Map = nova::hash_map<std::string, int, TransparentStringHash,
		TransparentStringEqual>;
	Map map;
	map.try_emplace("alpha", 10);
	map.try_emplace("beta", 20);

	const std::string_view alpha = "alpha";
	const std::string_view missing = "missing";
	Require(map.find(alpha) != map.end());
	Require(map.find(alpha)->second == 10);
	Require(map.contains(alpha));
	Require(map.count(alpha) == 1);
	Require(!map.contains(missing));

	const Map& const_map = map;
	Require(const_map.find(std::string_view("beta")) != const_map.end());
	Require(map.erase(alpha));
	Require(!map.contains(alpha));

	std::cout << "PASSED!\n";
}

void TestSingleProbeInsertion() {
	std::cout << "[Test 24] Single-probe insertion... " << std::flush;

	nova::hash_map<int, int, CountingHash> map;
	map.reserve(8);

	CountingHash::calls = 0;
	const auto [first, first_inserted] = map.try_emplace(1, 10);
	Require(first_inserted);
	Require(first->second == 10);
	Require(CountingHash::calls == 1);

	CountingHash::calls = 0;
	const auto [duplicate, duplicate_inserted] = map.try_emplace(1, 20);
	Require(!duplicate_inserted);
	Require(duplicate->second == 10);
	Require(CountingHash::calls == 1);

	CountingHash::calls = 0;
	const auto [assigned, assigned_inserted] = map.insert_or_assign(2, 30);
	Require(assigned_inserted);
	Require(assigned->second == 30);
	Require(CountingHash::calls == 1);

	std::cout << "PASSED!\n";
}

void TestDeferredConstructionExceptionSafety() {
	std::cout << "[Test 25] Deferred construction exception safety... "
			  << std::flush;

	nova::hash_map<int, ThrowOnConstruction> map;
	map.reserve(8);
	map.try_emplace(1, 10);

	ThrowOnConstruction::should_throw = true;
	const auto [duplicate, duplicate_inserted] = map.try_emplace(1, 20);
	Require(!duplicate_inserted);
	Require(duplicate->second.value == 10);

	bool threw = false;
	try {
		map.try_emplace(2, 20);
	} catch (const std::runtime_error&) {
		threw = true;
	}
	Require(threw);
	Require(map.size() == 1);
	Require(!map.contains(2));

	ThrowOnConstruction::should_throw = false;
	const auto [inserted, was_inserted] = map.try_emplace(2, 20);
	Require(was_inserted);
	Require(inserted->second.value == 20);

	std::cout << "PASSED!\n";
}

int main() {
  std::cout << "=== Running NovaContainers Tests ===\n\n";

  TestBasicOperations();
  TestLargeScaleRehash();
  TestIteration();
	TestConstLookupAndBoundsChecking();
	TestHashExceptionsPropagate();
	TestEqualityExceptionPreservesRehashSource();
	TestStatefulHashSurvivesCopyAndMove();
	TestRehashPreservesTableWhenCopyThrows();
	TestMoveOnlyRehashPreservesStructuralInvariants();
	TestTombstonesAreCompactedWithoutGrowth();
	TestControlTailWrapAround();
	TestExtendedContainerApi();
	TestDifferentialOperations();
	TestCollisionStressOperations();
	TestMemoryFootprintAccounting();
	TestTypeSpecificMemoryFootprints();
	TestStatefulAllocatorOwnership();
	TestPropagatingAllocatorOwnership();
	TestLifecycleAndCapacityControls();
	TestHashExceptionPreservesRehashSource();
	TestAllocatorMoveAssignmentException();
	TestLongStringPayloadMemory();
	TestHeterogeneousLookup();
	TestSingleProbeInsertion();
	TestDeferredConstructionExceptionSafety();
  std::cout << "\nAll NovaContainers tests completed successfully!\n";
  return 0;
}
