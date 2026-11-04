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

void require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

struct constant_hash {
	std::size_t operator()(int) const noexcept { return 0; }
};

struct transparent_string_hash {
	using is_transparent = void;

	std::size_t operator()(std::string_view value) const noexcept {
		return std::hash<std::string_view>{}(value);
	}

	std::size_t operator()(const std::string& value) const noexcept {
		return (*this)(std::string_view(value));
	}
};

struct transparent_string_equal {
	using is_transparent = void;

	bool operator()(std::string_view left, std::string_view right) const noexcept {
		return left == right;
	}
};

struct toggle_throw_hash {
	std::shared_ptr<bool> should_throw;

	std::size_t operator()(int value) const {
		if (*should_throw) {
			throw std::runtime_error("hash failure");
		}
		return std::hash<int>{}(value);
	}
};

struct allocation_state {
	std::size_t allocated_bytes = 0;
	std::size_t deallocated_bytes = 0;
};

template <typename T>
class tracking_allocator {
 public:
	using value_type = T;

	tracking_allocator() : state_(std::make_shared<allocation_state>()) {}
	explicit tracking_allocator(std::shared_ptr<allocation_state> state)
		: state_(std::move(state)) {}

	template <typename U>
	tracking_allocator(const tracking_allocator<U>& other) noexcept
		: state_(other.state()) {}

	T* allocate(std::size_t count) {
		state_->allocated_bytes += count * sizeof(T);
		return std::allocator<T>{}.allocate(count);
	}

	void deallocate(T* pointer, std::size_t count) noexcept {
		state_->deallocated_bytes += count * sizeof(T);
		std::allocator<T>{}.deallocate(pointer, count);
	}

	std::shared_ptr<allocation_state> state() const noexcept { return state_; }

	template <typename U>
	bool operator==(const tracking_allocator<U>& other) const noexcept {
		return state_ == other.state();
	}

	template <typename U>
	bool operator!=(const tracking_allocator<U>& other) const noexcept {
		return !(*this == other);
	}

 private:
	std::shared_ptr<allocation_state> state_;
};

void test_basic_operations() {
	nova::hash_set<int> values;
	require(values.empty(), "new set must be empty");
	require(values.capacity() == 0, "new set must defer allocation");

	const auto [first, inserted] = values.insert(42);
	require(inserted && *first == 42, "first insert must succeed");
	const auto [duplicate, inserted_duplicate] = values.insert(42);
	require(!inserted_duplicate && *duplicate == 42,
		"duplicate insert must return existing key");
	require(values.size() == 1, "duplicate must not change size");
	require(values.contains(42), "contains must find inserted key");
	require(values.count(42) == 1 && values.count(7) == 0,
		"count must be zero or one");
	require(values.erase(42), "erase must remove existing key");
	require(!values.erase(42), "erase must report missing key");
}

void test_initializer_iteration_and_erase() {
	nova::hash_set<int> values{1, 2, 3, 3, 4};
	require(values.size() == 4, "initializer list must remove duplicates");

	int sum = 0;
	for (const int value : values) {
		sum += value;
	}
	require(sum == 10, "iteration must visit every key once");

	auto position = values.find(2);
	require(position != values.end(), "find must return iterator");
	position = values.erase(position);
	require(!values.contains(2), "iterator erase must remove key");
	if (position != values.end()) {
		require(values.contains(*position), "erase return iterator must be valid");
	}

	static_assert(std::is_same_v<decltype(*values.begin()), const int&>);
}

void test_growth_reserve_and_rehash() {
	nova::hash_set<int> values;
	values.reserve(100000);
	const std::size_t reserved_capacity = values.capacity();
	require(reserved_capacity >= 100000, "reserve must provide requested room");

	for (int value = 0; value < 100000; ++value) {
		values.insert(value);
	}
	require(values.capacity() == reserved_capacity,
		"reserved insertion must not grow");
	for (int value = 0; value < 100000; ++value) {
		require(values.contains(value), "rehash test lost a key");
	}

	for (int value = 0; value < 50000; ++value) {
		values.erase(value);
	}
	values.rehash(values.capacity());
	require(values.size() == 50000, "same-capacity rehash must preserve size");
	for (int value = 50000; value < 100000; ++value) {
		require(values.contains(value), "tombstone cleanup lost a key");
	}
}

void test_collision_and_wrap_around() {
	nova::hash_set<int, constant_hash> values;
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
		require(values.contains(value), "collision probe lost existing key");
	}
	for (int value = 200; value < 400; ++value) {
		require(values.contains(value), "collision insert lost new key");
	}
}

void test_copy_move_and_clear() {
	nova::hash_set<std::string> original{"alpha", "beta", "gamma"};
	nova::hash_set<std::string> copy(original);
	require(copy.size() == original.size() && copy.contains("beta"),
		"copy construction must preserve keys");

	nova::hash_set<std::string> assigned;
	assigned = copy;
	require(assigned.contains("gamma"), "copy assignment must preserve keys");

	nova::hash_set<std::string> moved(std::move(copy));
	require(moved.contains("alpha"), "move construction must preserve keys");
	require(copy.empty(), "moved-from set must be empty");

	nova::hash_set<std::string> move_assigned;
	move_assigned = std::move(assigned);
	require(move_assigned.contains("beta"),
		"move assignment must preserve keys");
	require(assigned.empty(), "move-assigned source must be empty");

	move_assigned.clear();
	require(move_assigned.empty(), "clear must remove every key");
	move_assigned.rehash(0);
	require(move_assigned.capacity() == 0, "rehash(0) must release empty storage");
}

void test_heterogeneous_lookup() {
	using set_type = nova::hash_set<std::string, transparent_string_hash,
		transparent_string_equal>;
	set_type values{"alpha", "beta", "gamma"};
	const std::string_view beta = "beta";
	require(values.contains(beta), "heterogeneous contains must find key");
	require(values.find(beta) != values.end(),
		"heterogeneous find must find key");
	require(values.count(beta) == 1, "heterogeneous count must find key");
	require(values.erase(beta), "heterogeneous erase must remove key");
	require(!values.contains(beta), "heterogeneous erase must persist");
}

void test_hash_exception_preserves_rehash_source() {
	const std::shared_ptr<bool> should_throw = std::make_shared<bool>(false);
	nova::hash_set<int, toggle_throw_hash> values(
		0, toggle_throw_hash{should_throw});
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
	require(threw, "throwing hash must propagate from rehash");
	require(values.size() == 100 && values.capacity() == old_capacity,
		"failed rehash must preserve source table");
	for (int value = 0; value < 100; ++value) {
		require(values.contains(value), "failed rehash lost a key");
	}
}

void test_differential_operations() {
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
				require(actual_inserted == expected_inserted,
					"differential insert result mismatch");
				break;
			}
			case 1:
				require(actual.erase(key) == (expected.erase(key) != 0),
					"differential erase result mismatch");
				break;
			case 2:
				require(actual.contains(key) == expected.contains(key),
					"differential lookup mismatch");
				break;
			default:
				actual.reserve(expected.size() + 128);
				expected.reserve(expected.size() + 128);
				break;
		}
		require(actual.size() == expected.size(),
			"differential size mismatch");
	}
	for (const int key : expected) {
		require(actual.contains(key), "differential final state mismatch");
	}
}

void test_direct_key_storage_size() {
	const std::shared_ptr<allocation_state> state =
		std::make_shared<allocation_state>();
	using allocator_type = tracking_allocator<int>;
	{
		nova::hash_set<int, std::hash<int>, std::equal_to<int>, allocator_type> values(
			0, std::hash<int>{}, std::equal_to<int>{}, allocator_type(state));
		values.reserve(100000);
		require(state->allocated_bytes == values.capacity() * sizeof(int),
			"hash_set slot allocation must store only Key bytes");
	}
	require(state->allocated_bytes == state->deallocated_bytes,
		"hash_set slot allocation and deallocation must balance");
}

void test_allocator_ownership() {
	const std::shared_ptr<allocation_state> first_state =
		std::make_shared<allocation_state>();
	const std::shared_ptr<allocation_state> second_state =
		std::make_shared<allocation_state>();
	using allocator_type = tracking_allocator<int>;
	using set_type = nova::hash_set<int, std::hash<int>, std::equal_to<int>, allocator_type>;
	{
		set_type source(0, std::hash<int>{}, std::equal_to<int>{},
			allocator_type(first_state));
		set_type target(0, std::hash<int>{}, std::equal_to<int>{},
			allocator_type(second_state));
		for (int value = 0; value < 100; ++value) {
			source.insert(value);
		}
		target.insert(-1);
		target = source;
		require(target.get_allocator().state() == second_state,
			"copy assignment must preserve a non-propagating allocator");
		require(target.size() == source.size() && target.contains(42),
			"copy assignment with unequal allocators lost keys");

		set_type move_target(0, std::hash<int>{}, std::equal_to<int>{},
			allocator_type(second_state));
		move_target = std::move(source);
		require(move_target.get_allocator().state() == second_state,
			"move assignment must preserve a non-propagating allocator");
		require(move_target.contains(42) && source.empty(),
			"move assignment with unequal allocators lost keys");

		bool swap_threw = false;
		try {
			move_target.swap(source);
		} catch (const std::invalid_argument&) {
			swap_threw = true;
		}
		require(swap_threw, "swap must reject unequal non-propagating allocators");
	}
	require(first_state->allocated_bytes == first_state->deallocated_bytes,
		"first allocator ownership must balance");
	require(second_state->allocated_bytes == second_state->deallocated_bytes,
		"second allocator ownership must balance");
}

}  // namespace

int main() {
	try {
		test_basic_operations();
		test_initializer_iteration_and_erase();
		test_growth_reserve_and_rehash();
		test_collision_and_wrap_around();
		test_copy_move_and_clear();
		test_heterogeneous_lookup();
		test_hash_exception_preserves_rehash_source();
		test_differential_operations();
		test_direct_key_storage_size();
		test_allocator_ownership();
		std::cout << "All nova::hash_set tests passed.\n";
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "hash_set test failure: " << exception.what() << '\n';
		return 1;
	}
}
