#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include "NovaContainers/hash_set.hpp"

namespace {

void Require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct ConstantHash {
	std::size_t operator()(int) const noexcept { return 0; }
};

struct TransparentStringHash {
	using is_transparent = void;

	std::size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}

	std::size_t operator()(const std::string& value) const noexcept {
		return (*this)(std::string_view(value));
	}
};

struct TransparentStringEqual {
	using is_transparent = void;

	bool operator()(std::string_view left, std::string_view right) const noexcept {
		return left == right;
	}
};

struct ToggleThrowHash {
	std::shared_ptr<bool> should_throw;

	std::size_t operator()(int value) const {
		if (*should_throw) {
			throw std::runtime_error("hash failure");
		}
		return std::hash<int>{}(value);
	}
};

struct AllocationState {
	std::size_t allocated_bytes = 0;
	std::size_t deallocated_bytes = 0;
};

template <typename T>
class TrackingAllocator {
 public:
	using value_type = T;

	TrackingAllocator() : state_(std::make_shared<AllocationState>()) {}
	explicit TrackingAllocator(std::shared_ptr<AllocationState> state)
		: state_(std::move(state)) {}

	template <typename U>
	TrackingAllocator(const TrackingAllocator<U>& other) noexcept
		: state_(other.state()) {}

	T* allocate(std::size_t count) {
		state_->allocated_bytes += count * sizeof(T);
		return std::allocator<T>{}.allocate(count);
	}

	void deallocate(T* pointer, std::size_t count) noexcept {
		state_->deallocated_bytes += count * sizeof(T);
		std::allocator<T>{}.deallocate(pointer, count);
	}

	std::shared_ptr<AllocationState> state() const noexcept { return state_; }

	template <typename U>
	bool operator==(const TrackingAllocator<U>& other) const noexcept {
		return state_ == other.state();
	}

	template <typename U>
	bool operator!=(const TrackingAllocator<U>& other) const noexcept {
		return !(*this == other);
	}

 private:
	std::shared_ptr<AllocationState> state_;
};

void TestBasicOperations() {
	nova::hash_set<int> values;
	Require(values.empty(), "new set must be empty");
	Require(values.capacity() == 0, "new set must defer allocation");

	const auto [first, inserted] = values.insert(42);
	Require(inserted && *first == 42, "first insert must succeed");
	const auto [duplicate, inserted_duplicate] = values.insert(42);
	Require(!inserted_duplicate && *duplicate == 42,
		"duplicate insert must return existing key");
	Require(values.size() == 1, "duplicate must not change size");
	Require(values.contains(42), "contains must find inserted key");
	Require(values.count(42) == 1 && values.count(7) == 0,
		"count must be zero or one");
	Require(values.erase(42), "erase must remove existing key");
	Require(!values.erase(42), "erase must report missing key");
}

void TestInitializerIterationAndErase() {
	nova::hash_set<int> values{1, 2, 3, 3, 4};
	Require(values.size() == 4, "initializer list must remove duplicates");

	int sum = 0;
	for (const int value : values) {
		sum += value;
	}
	Require(sum == 10, "iteration must visit every key once");

	auto position = values.find(2);
	Require(position != values.end(), "find must return iterator");
	position = values.erase(position);
	Require(!values.contains(2), "iterator erase must remove key");
	if (position != values.end()) {
		Require(values.contains(*position), "erase return iterator must be valid");
	}

	static_assert(std::is_same_v<decltype(*values.begin()), const int&>);
}

void TestGrowthReserveAndRehash() {
	nova::hash_set<int> values;
	values.reserve(100000);
	const std::size_t reserved_capacity = values.capacity();
	Require(reserved_capacity >= 100000, "reserve must provide requested room");

	for (int value = 0; value < 100000; ++value) {
		values.insert(value);
	}
	Require(values.capacity() == reserved_capacity,
		"reserved insertion must not grow");
	for (int value = 0; value < 100000; ++value) {
		Require(values.contains(value), "rehash test lost a key");
	}

	for (int value = 0; value < 50000; ++value) {
		values.erase(value);
	}
	values.rehash(values.capacity());
	Require(values.size() == 50000, "same-capacity rehash must preserve size");
	for (int value = 50000; value < 100000; ++value) {
		Require(values.contains(value), "tombstone cleanup lost a key");
	}
}

void TestCollisionAndWrapAround() {
	nova::hash_set<int, ConstantHash> values;
	for (int value = 0; value < 200; ++value) {
		values.insert(value);
	}
	for (int value = 0; value < 200; value += 2) {
		values.erase(value);
	}
	for (int value = 200; value < 400; ++value) {
		values.insert(value);
	}
	for (int value = 1; value < 200; value += 2) {
		Require(values.contains(value), "collision probe lost existing key");
	}
	for (int value = 200; value < 400; ++value) {
		Require(values.contains(value), "collision insert lost new key");
	}
}

void TestCopyMoveAndClear() {
	nova::hash_set<std::string> original{"alpha", "beta", "gamma"};
	nova::hash_set<std::string> copy(original);
	Require(copy.size() == original.size() && copy.contains("beta"),
		"copy construction must preserve keys");

	nova::hash_set<std::string> assigned;
	assigned = copy;
	Require(assigned.contains("gamma"), "copy assignment must preserve keys");

	nova::hash_set<std::string> moved(std::move(copy));
	Require(moved.contains("alpha"), "move construction must preserve keys");
	Require(copy.empty(), "moved-from set must be empty");

	nova::hash_set<std::string> move_assigned;
	move_assigned = std::move(assigned);
	Require(move_assigned.contains("beta"),
		"move assignment must preserve keys");
	Require(assigned.empty(), "move-assigned source must be empty");

	move_assigned.clear();
	Require(move_assigned.empty(), "clear must remove every key");
	move_assigned.rehash(0);
	Require(move_assigned.capacity() == 0, "rehash(0) must release empty storage");
}

void TestHeterogeneousLookup() {
	using Set = nova::hash_set<std::string, TransparentStringHash,
		TransparentStringEqual>;
	Set values{"alpha", "beta", "gamma"};
	const std::string_view beta = "beta";
	Require(values.contains(beta), "heterogeneous contains must find key");
	Require(values.find(beta) != values.end(),
		"heterogeneous find must find key");
	Require(values.count(beta) == 1, "heterogeneous count must find key");
	Require(values.erase(beta), "heterogeneous erase must remove key");
	Require(!values.contains(beta), "heterogeneous erase must persist");
}

void TestHashExceptionPreservesRehashSource() {
	const std::shared_ptr<bool> should_throw = std::make_shared<bool>(false);
	nova::hash_set<int, ToggleThrowHash> values(
		0, ToggleThrowHash{should_throw});
	for (int value = 0; value < 100; ++value) {
		values.insert(value);
	}
	const std::size_t old_capacity = values.capacity();
	*should_throw = true;
	bool threw = false;
	try {
		values.rehash(old_capacity * 2);
	} catch (const std::runtime_error&) {
		threw = true;
	}
	*should_throw = false;
	Require(threw, "throwing hash must propagate from rehash");
	Require(values.size() == 100 && values.capacity() == old_capacity,
		"failed rehash must preserve source table");
	for (int value = 0; value < 100; ++value) {
		Require(values.contains(value), "failed rehash lost a key");
	}
}

void TestDifferentialOperations() {
	nova::hash_set<int> actual;
	std::unordered_set<int> expected;
	uint32_t state = 0xA341316CU;
	for (int operation = 0; operation < 100000; ++operation) {
		state = state * 1664525U + 1013904223U;
		const int key = static_cast<int>((state >> 8) % 4096U);
		switch (state & 3U) {
			case 0: {
				const bool actual_inserted = actual.insert(key).second;
				const bool expected_inserted = expected.insert(key).second;
				Require(actual_inserted == expected_inserted,
					"differential insert result mismatch");
				break;
			}
			case 1:
				Require(actual.erase(key) == (expected.erase(key) != 0),
					"differential erase result mismatch");
				break;
			case 2:
				Require(actual.contains(key) == expected.contains(key),
					"differential lookup mismatch");
				break;
			default:
				actual.reserve(expected.size() + 128);
				expected.reserve(expected.size() + 128);
				break;
		}
		Require(actual.size() == expected.size(),
			"differential size mismatch");
	}
	for (const int key : expected) {
		Require(actual.contains(key), "differential final state mismatch");
	}
}

void TestDirectKeyStorageSize() {
	const std::shared_ptr<AllocationState> state =
		std::make_shared<AllocationState>();
	using Allocator = TrackingAllocator<int>;
	{
		nova::hash_set<int, std::hash<int>, std::equal_to<int>, Allocator> values(
			0, std::hash<int>{}, std::equal_to<int>{}, Allocator(state));
		values.reserve(100000);
		Require(state->allocated_bytes == values.capacity() * sizeof(int),
			"hash_set slot allocation must store only Key bytes");
	}
	Require(state->allocated_bytes == state->deallocated_bytes,
		"hash_set slot allocation and deallocation must balance");
}

void TestAllocatorOwnership() {
	const std::shared_ptr<AllocationState> first_state =
		std::make_shared<AllocationState>();
	const std::shared_ptr<AllocationState> second_state =
		std::make_shared<AllocationState>();
	using Allocator = TrackingAllocator<int>;
	using Set = nova::hash_set<int, std::hash<int>, std::equal_to<int>, Allocator>;
	{
		Set source(0, std::hash<int>{}, std::equal_to<int>{},
			Allocator(first_state));
		Set target(0, std::hash<int>{}, std::equal_to<int>{},
			Allocator(second_state));
		for (int value = 0; value < 100; ++value) {
			source.insert(value);
		}
		target.insert(-1);
		target = source;
		Require(target.get_allocator().state() == second_state,
			"copy assignment must preserve a non-propagating allocator");
		Require(target.size() == source.size() && target.contains(42),
			"copy assignment with unequal allocators lost keys");

		Set move_target(0, std::hash<int>{}, std::equal_to<int>{},
			Allocator(second_state));
		move_target = std::move(source);
		Require(move_target.get_allocator().state() == second_state,
			"move assignment must preserve a non-propagating allocator");
		Require(move_target.contains(42) && source.empty(),
			"move assignment with unequal allocators lost keys");

		bool swap_threw = false;
		try {
			move_target.Swap(source);
		} catch (const std::invalid_argument&) {
			swap_threw = true;
		}
		Require(swap_threw, "swap must reject unequal non-propagating allocators");
	}
	Require(first_state->allocated_bytes == first_state->deallocated_bytes,
		"first allocator ownership must balance");
	Require(second_state->allocated_bytes == second_state->deallocated_bytes,
		"second allocator ownership must balance");
}

}  // namespace

int main() {
	try {
		TestBasicOperations();
		TestInitializerIterationAndErase();
		TestGrowthReserveAndRehash();
		TestCollisionAndWrapAround();
		TestCopyMoveAndClear();
		TestHeterogeneousLookup();
		TestHashExceptionPreservesRehashSource();
		TestDifferentialOperations();
		TestDirectKeyStorageSize();
		TestAllocatorOwnership();
		std::cout << "All nova::hash_set tests passed.\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "hash_set test failure: " << exception.what() << '\n';
		return 1;
	}
}
