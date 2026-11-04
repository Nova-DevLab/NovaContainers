#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "NovaContainers/nova_containers.hpp"

namespace {

void require(bool condition, const char* message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

template <typename Actual, typename Expected>
void require_equal(const Actual& actual, const Expected& expected) {
	require(actual.size() == expected.size(), "list size mismatch");
	require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()),
		"list sequence mismatch");
	require(std::equal(actual.rbegin(), actual.rend(), expected.rbegin(), expected.rend()),
		"list reverse sequence mismatch");
	std::size_t index = 0;
	for (auto block : actual.blocks()) {
		require(!block.empty(), "blocks() exposed an empty block");
		for (int value : block) {
			require(value == expected[index++], "blocks() sequence mismatch");
		}
	}
	require(index == actual.size(), "blocks() element count mismatch");
	if (!actual.empty()) {
		require(actual.front() == expected.front(), "front mismatch");
		require(actual.back() == expected.back(), "back mismatch");
		for (std::size_t i = 0; i < actual.size(); ++i) {
			require(actual[i] == expected[i], "index mismatch");
		}
	}
}

void test_basic_and_iterators() {
	using list_type = nova::list<int>;
	static_assert(std::random_access_iterator<list_type::iterator>);
	static_assert(std::random_access_iterator<list_type::const_iterator>);
	static_assert(std::ranges::random_access_range<list_type>);
	static_assert(!std::contiguous_iterator<list_type::iterator>);	static_assert(std::is_convertible_v<list_type::iterator, list_type::const_iterator>);
	static_assert(!std::is_convertible_v<list_type::const_iterator, list_type::iterator>);
	list_type values;
	require(values.empty(), "default list is not empty");
	require(values.begin() == values.end(), "empty iterators differ");
	require(values.end() - values.begin() == 0, "empty iterator distance mismatch");
	require(values.begin() + 0 == values.end(), "empty iterator addition mismatch");
	std::size_t empty_blocks = 0;
	for ([[maybe_unused]] auto block : values.blocks()) {
		++empty_blocks;
	}
	require(empty_blocks == 0, "empty list exposed occupied blocks");
	bool threw = false;
	try {
		(void)values.at(0);
	} catch (const std::out_of_range&) {
		threw = true;
	}
	require(threw, "at() did not reject out-of-range index");

	values.push_back(2);
	values.push_front(1);
	values.emplace_back(3);
	values.emplace_front(0);
	require_equal(values, std::vector<int>{0, 1, 2, 3});
	const list_type& constant = values;
	list_type::const_iterator first = values.begin();
	require(first == values.begin() && values.begin() == first,
		"mixed iterator equality mismatch");
	require(constant.end() - values.begin() == 4,
		"mixed iterator subtraction mismatch");
	require(values.end() - first == 4, "reverse mixed distance mismatch");
	require(values.begin() < constant.end() && first < values.end(),
		"mixed iterator ordering mismatch");
	require(*(2 + values.begin()) == 2 && values.begin()[3] == 3,
		"random-access iterator operations mismatch");
	require(constant.at(2) == 2, "const at() mismatch");
	require(*--values.end() == 3, "end decrement mismatch");
	values.pop_front();
	values.pop_back();
	require_equal(values, std::vector<int>{1, 2});
	values.clear();
	require(values.empty(), "clear did not empty list");
	values.emplace_back(42);
	require_equal(values, std::vector<int>{42});
}

void test_boundaries_and_stable_addresses() {
	nova::list<int> values;
	values.reserve_back(30000);
	values.reserve_front(30000);
	values.emplace_back(1000000);
	int* anchor = std::addressof(values.front());
	for (int i = 0; i < 25000; ++i) {
		values.emplace_back(i);
		values.emplace_front(-i - 1);
	}
	require(std::addressof(values[25000]) == anchor && *anchor == 1000000,
		"end insertion invalidated existing element address");
	std::deque<int> expected;
	for (int i = 25000; i > 0; --i) {
		expected.push_back(-i);
	}
	expected.push_back(1000000);
	for (int i = 0; i < 25000; ++i) {
		expected.push_back(i);
	}
	require_equal(values, expected);
	for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(values.size()); i += 31) {
		auto position = values.begin() + i;
		require(position - values.begin() == i && *position == values[static_cast<std::size_t>(i)],
			"iterator block jump mismatch");
		require(values.end() - position == static_cast<std::ptrdiff_t>(values.size()) - i,
			"iterator distance across blocks mismatch");
		position -= i;
		require(position == values.begin(), "negative iterator jump mismatch");
	}
	for (int i = 0; i < 20000; ++i) {
		values.pop_front();
		values.pop_back();
		expected.pop_front();
		expected.pop_back();
	}
	require_equal(values, expected);
	for (auto block : values.blocks()) {
		for (int& value : block) {
			value += 3;
		}
	}
	for (int& value : expected) {
		value += 3;
	}
	require_equal(values, expected);

	values.clear();
	for (int i = 0; i < 60000; ++i) {
		values.push_front(i);
	}
	for (int i = 59999; i >= 0; --i) {
		require(values.front() == i, "front-only growth order mismatch");
		values.pop_front();
	}
	require(values.empty(), "front-only drain failed");
	for (int i = 0; i < 100000; ++i) {
		values.push_back(i);
		require(values.front() == i, "sliding queue mismatch");
		values.pop_front();
	}
	require(values.empty(), "sliding queue drain failed");
}

void test_insert_erase_and_ranges() {
	nova::list<int> values{1, 3, 5};
	auto inserted = values.insert(values.cbegin() + 1, 2);
	require(*inserted == 2, "insert returned wrong iterator");
	inserted = values.emplace(values.cbegin() + 3, 4);
	require(*inserted == 4, "emplace returned wrong iterator");
	require_equal(values, std::vector<int>{1, 2, 3, 4, 5});
	values.insert(values.cbegin(), 2, -1);
	const std::array tail{6, 7, 8};
	values.insert(values.cend(), tail.begin(), tail.end());
	require_equal(values, std::vector<int>{-1, -1, 1, 2, 3, 4, 5, 6, 7, 8});
	auto next = values.erase(values.cbegin() + 1);
	require(*next == 1, "erase returned wrong iterator");
	next = values.erase(values.cbegin() + 2, values.cbegin() + 6);
	require(*next == 6, "range erase returned wrong iterator");
	require_equal(values, std::vector<int>{-1, 1, 6, 7, 8});
	require(values.erase(values.cend(), values.cend()) == values.end(),
		"empty range erase returned wrong iterator");
	require(values.insert(values.cbegin() + 2, 0, 99) == values.begin() + 2,
		"zero-count insertion returned wrong iterator");
	values.assign(tail.begin(), tail.end());
	require_equal(values, tail);
	values.assign(4, 9);
	require_equal(values, std::vector<int>{9, 9, 9, 9});
	values.resize(2);
	values.resize(6, 11);
	require_equal(values, std::vector<int>{9, 9, 11, 11, 11, 11});
	values.resize(8);
	require(values[6] == 0 && values[7] == 0, "resize did not value-initialize");
	values.erase(values.begin(), values.end());
	require(values.empty(), "erase all failed");

	std::vector<int> input(17000);
	std::iota(input.begin(), input.end(), 0);
	values.assign(input.begin(), input.end());
	values.insert(values.begin() + 8000, input.begin(), input.end());
	input.insert(input.begin() + 8000, 17000, 0);
	std::iota(input.begin() + 8000, input.begin() + 25000, 0);
	require_equal(values, input);
	values.erase(values.begin() + 4000, values.begin() + 26000);
	input.erase(input.begin() + 4000, input.begin() + 26000);
	require_equal(values, input);
}

void test_copy_move_and_sort() {
	std::mt19937 random(123456);
	std::vector<int> input(35000);
	for (int& value : input) {
		value = static_cast<int>(random() % 10000);
	}
	nova::list<int> values(input.begin(), input.end());
	nova::list<int> copied(values);
	values[0] = -1;
	require(copied[0] == input[0], "copy shared element storage");
	nova::list<int> assigned;
	assigned = copied;
	require_equal(assigned, input);
	nova::list<int> moved(std::move(copied));
	require_equal(moved, input);
	copied.emplace_back(7);
	require(copied.back() == 7, "moved-from list cannot be reused");
	assigned = std::move(moved);
	require_equal(assigned, input);
	std::sort(assigned.begin(), assigned.end());
	std::sort(input.begin(), input.end());
	require_equal(assigned, input);
	std::ranges::sort(assigned, std::greater<>{});
	std::reverse(input.begin(), input.end());
	require_equal(assigned, input);
	nova::list<int> other{1, 2};
	assigned.swap(other);
	require_equal(assigned, std::vector<int>{1, 2});
	require_equal(other, input);
	assigned.swap(other);
	require_equal(assigned, input);
	assigned = assigned;
	require_equal(assigned, input);
}

void test_random_differential() {
	std::mt19937 random(0x4E4F5641);
	nova::list<int> actual;
	std::deque<int> expected;
	for (int step = 0; step < 40000; ++step) {
		const int operation = static_cast<int>(random() % 10);
		const int value = static_cast<int>(random() % 100000);
		const std::size_t position = expected.empty() ? 0 : random() % (expected.size() + 1);
		switch (operation) {
			case 0:
				actual.push_front(value);
				expected.push_front(value);
				break;
			case 1:
			case 2:
				actual.push_back(value);
				expected.push_back(value);
				break;
			case 3:
				actual.insert(actual.begin() + static_cast<std::ptrdiff_t>(position), value);
				expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(position), value);
				break;
			case 4:
				if (!expected.empty()) {
					actual.pop_front();
					expected.pop_front();
				}
				break;
			case 5:
				if (!expected.empty()) {
					actual.pop_back();
					expected.pop_back();
				}
				break;
			case 6:
				if (position < expected.size()) {
					actual.erase(actual.begin() + static_cast<std::ptrdiff_t>(position));
					expected.erase(expected.begin() + static_cast<std::ptrdiff_t>(position));
				}
				break;
			case 7: {
				const std::size_t stop = position + random() % (expected.size() - position + 1);
				actual.erase(actual.begin() + static_cast<std::ptrdiff_t>(position),
					actual.begin() + static_cast<std::ptrdiff_t>(stop));
				expected.erase(expected.begin() + static_cast<std::ptrdiff_t>(position),
					expected.begin() + static_cast<std::ptrdiff_t>(stop));
				break;
			}
			case 8:
				actual.reserve_front(random() % 200);
				actual.reserve_back(random() % 200);
				break;
			default:
				if (position < expected.size()) {
					actual[position] = value;
					expected[position] = value;
				}
				break;
		}
		if (step % 71 == 0) {
			require_equal(actual, expected);
		}
	}
	require_equal(actual, expected);
}

struct tracked {
	inline static int live = 0;
	inline static int construction_budget = -1;
	int value;

	explicit tracked(int initial = 0) : value(initial) {
		if (construction_budget == 0) {
			throw std::runtime_error("requested construction failure");
		}
		if (construction_budget > 0) {
			--construction_budget;
		}
		++live;
	}
	tracked(const tracked& other) : tracked(other.value) {}
	tracked(tracked&& other) noexcept : value(std::exchange(other.value, -1)) { ++live; }
	tracked& operator=(const tracked&) = default;
	tracked& operator=(tracked&& other) noexcept {
		value = std::exchange(other.value, -1);
		return *this;
	}
	~tracked() { --live; }
};

void test_move_only_and_lifetime() {
	{
		nova::list<std::unique_ptr<int>> values;
		for (int i = 0; i < 4000; ++i) {
			values.emplace_back(std::make_unique<int>(i));
		}
		values.emplace_front(std::make_unique<int>(-1));
		values.insert(values.begin() + 100, std::make_unique<int>(-2));
		require(*values[100] == -2 && *values[101] == 99, "move-only middle insertion failed");
		values.erase(values.begin() + 100);
		require(*values[100] == 99, "move-only middle erase failed");
		std::sort(values.begin(), values.end(),
			[](const auto& left, const auto& right) { return *left > *right; });
		require(*values.front() == 3999 && *values.back() == -1, "move-only sort failed");
	}
	require(tracked::live == 0, "initial lifetime count mismatch");
	{
		nova::list<tracked> values;
		values.reserve_front(5000);
		values.reserve_back(5000);
		require(tracked::live == 0, "reserve constructed unused elements");
		for (int i = 0; i < 5000; ++i) {
			values.emplace_back(i);
		}
		values.emplace(values.begin() + 200, -1);
		require(tracked::live == static_cast<int>(values.size()), "insert leaked live objects");
		values.erase(values.begin() + 100, values.begin() + 4000);
		require(tracked::live == static_cast<int>(values.size()), "erase leaked live objects");
		const std::size_t previous_size = values.size();
		tracked::construction_budget = 0;
		bool threw = false;
		try {
			values.emplace_front(7);
		} catch (const std::runtime_error&) {
			threw = true;
		}
		tracked::construction_budget = -1;
		require(threw && values.size() == previous_size,
			"throwing front construction changed size");
		require(tracked::live == static_cast<int>(values.size()),
			"throwing construction leaked live objects");
		tracked::construction_budget = 3;
		threw = false;
		try {
			nova::list<tracked> copy(values);
		} catch (const std::runtime_error&) {
			threw = true;
		}
		tracked::construction_budget = -1;
		require(threw && tracked::live == static_cast<int>(values.size()),
			"failed copy leaked constructed elements");
		values.clear();
		require(tracked::live == 0, "clear failed to destroy elements");
	}
	require(tracked::live == 0, "list destruction leaked elements");
}


void test_aliasing_and_throwing_moves() {
	nova::list<int> values{1, 2, 3, 4};
	values.insert(values.begin() + 1, values.back());
	require_equal(values, std::vector<int>{1, 4, 2, 3, 4});
	values.insert(values.end() - 1, values.front());
	require_equal(values, std::vector<int>{1, 4, 2, 3, 1, 4});
	values.resize(10000, values[1]);
	require(values.back() == 4, "resize with aliased fill value failed");
	values.assign(12000, values.back());
	require(values.size() == 12000 && values.front() == 4 && values.back() == 4,
		"assign with aliased fill value failed");
}

void test_count_insert_aliasing_and_copy_failure() {
	for (bool optimized : {false, true}) {
		for (std::size_t position : {std::size_t{0}, std::size_t{2}, std::size_t{8}, std::size_t{10}}) {
			nova::list<int, std::allocator<int>, 16> values;
			std::vector<int> expected;
			for (int i = 0; i < 10; ++i) {
				values.push_back(i);
				expected.push_back(i);
			}
			if (optimized) values.optimize();
			const int fill = values[3];
			const auto inserted = values.insert(values.begin() + static_cast<std::ptrdiff_t>(position),
				13, values[3]);
			expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(position), 13, fill);
			require_equal(values, expected);
			require(inserted == values.begin() + static_cast<std::ptrdiff_t>(position),
				"count insertion returned the wrong position");

			for (int budget : {0, 3, 12}) {
				{
					nova::list<tracked, std::allocator<tracked>, 16> throwing;
					for (int i = 0; i < 10; ++i) throwing.emplace_back(i);
					if (optimized) throwing.optimize();
					std::array<const tracked*, 10> addresses;
					for (std::size_t i = 0; i < addresses.size(); ++i) addresses[i] = &throwing[i];
					tracked::construction_budget = budget;
					bool threw = false;
					try {
						throwing.insert(throwing.begin() + static_cast<std::ptrdiff_t>(position),
							13, throwing[3]);
					} catch (const std::runtime_error&) {
						threw = true;
					}
					tracked::construction_budget = -1;
					require(threw && throwing.size() == addresses.size(),
						"failed count copy changed size");
					for (std::size_t i = 0; i < addresses.size(); ++i) {
						require(&throwing[i] == addresses[i] && throwing[i].value == static_cast<int>(i),
							"failed count copy changed an original value or address");
					}
					require(tracked::live == 10, "failed count copy leaked live elements");
					tracked::construction_budget = 0;
					const auto unchanged = throwing.insert(throwing.begin() + 2, 0, throwing[3]);
					tracked::construction_budget = -1;
					require(unchanged == throwing.begin() + 2 && throwing.size() == 10,
						"empty count insertion constructed an element");
					throwing.emplace_back(42);
					require(throwing.back().value == 42, "failed count copy prevented reuse");
				}
				require(tracked::live == 0, "count insertion destruction leaked elements");
			}
		}
	}
}

struct throwing_move {
	inline static int live = 0;
	inline static int budget = -1;
	int value;
	explicit throwing_move(int initial = 0) : value(initial) { ++live; }
	throwing_move(const throwing_move&) = delete;
	throwing_move& operator=(const throwing_move&) = delete;
	static void consume() {
		if (budget == 0) {
			throw std::runtime_error("requested move failure");
		}
		if (budget > 0) {
			--budget;
		}
	}
	throwing_move(throwing_move&& other) : value(other.value) {
		consume();
		other.value = -1;
		++live;
	}
	throwing_move& operator=(throwing_move&& other) {
		consume();
		value = std::exchange(other.value, -1);
		return *this;
	}
	~throwing_move() { --live; }
};

void test_throwing_move_basic_guarantee() {
	for (int operation = 0; operation < 2; ++operation) {
		for (int budget : {0, 1, 3}) {
			{
				nova::list<throwing_move> values;
				for (int i = 0; i < 100; ++i) {
					values.emplace_back(i);
				}
				throwing_move::budget = budget;
				bool threw = false;
				try {
					if (operation == 0) {
						values.emplace(values.begin() + 20, -2);
					} else {
						values.erase(values.begin() + 20);
					}
				} catch (const std::runtime_error&) {
					threw = true;
				}
				throwing_move::budget = -1;
				require(threw, "throwing move path was not exercised");
				require(throwing_move::live == static_cast<int>(values.size()),
					"failed move left untracked live objects");
				std::size_t visited = 0;
				for ([[maybe_unused]] const auto& value : values) {
					++visited;
				}
				require(visited == values.size(), "failed move corrupted iteration");
				values.clear();
				require(throwing_move::live == 0, "failed move caused a lifetime leak");
				values.emplace_front(9);
				require(values.front().value == 9, "list unusable after move failure");
			}
			require(throwing_move::live == 0, "throwing move destruction leaked objects");
		}
	}
}


void test_throwing_range_insert() {
	for (int budget : {0, 3, 7, 8, 12}) {
		{
			nova::list<throwing_move, std::allocator<throwing_move>, 16> values;
			std::vector<throwing_move> input;
			for (int i = 0; i < 40; ++i) {
				values.emplace_back(i);
			}
			for (int i = 0; i < 7; ++i) {
				input.emplace_back(100 + i);
			}
			throwing_move::budget = budget;
			bool threw = false;
			try {
				values.insert(values.begin() + 15,
					std::make_move_iterator(input.begin()), std::make_move_iterator(input.end()));
			} catch (const std::runtime_error&) {
				threw = true;
			}
			throwing_move::budget = -1;
			require(threw, "throwing range move path was not exercised");
			require(throwing_move::live == static_cast<int>(values.size() + input.size()),
				"range insert failure leaked live elements");
			values.clear();
			values.emplace_back(9);
			require(values.back().value == 9, "range insert failure prevented reuse");
		}
		require(throwing_move::live == 0, "range insert failure leaked objects");
	}
}

struct allocation_state {
	std::map<void*, std::size_t> allocations;
	int budget = -1;
};

template <typename T, bool Propagate>
struct tracking_allocator {
	using value_type = T;
	using propagate_on_container_copy_assignment = std::bool_constant<Propagate>;
	using propagate_on_container_move_assignment = std::bool_constant<Propagate>;
	using propagate_on_container_swap = std::bool_constant<Propagate>;
	using is_always_equal = std::false_type;
	template <typename U> struct rebind { using other = tracking_allocator<U, Propagate>; };
	std::shared_ptr<allocation_state> state = std::make_shared<allocation_state>();

	tracking_allocator() = default;
	explicit tracking_allocator(std::shared_ptr<allocation_state> initial) : state(std::move(initial)) {}
	template <typename U>
	tracking_allocator(const tracking_allocator<U, Propagate>& other) noexcept : state(other.state) {}
	T* allocate(std::size_t count) {
		if (state->budget == 0) {
			throw std::bad_alloc();
		}
		if (state->budget > 0) {
			--state->budget;
		}
		T* result = std::allocator<T>{}.allocate(count);
		state->allocations.emplace(result, count);
		return result;
	}
	void deallocate(T* pointer, std::size_t count) noexcept {
		const auto found = state->allocations.find(pointer);
		if (found == state->allocations.end() || found->second != count) {
			std::terminate();
		}
		state->allocations.erase(found);
		std::allocator<T>{}.deallocate(pointer, count);
	}
	template <typename U>
	bool operator==(const tracking_allocator<U, Propagate>& other) const noexcept {
		return state == other.state;
	}
};

template <bool Propagate>
void test_allocator_propagation() {
	using allocator_type = tracking_allocator<int, Propagate>;
	using list_type = nova::list<int, allocator_type>;
	auto first = std::make_shared<allocation_state>();
	auto second = std::make_shared<allocation_state>();
	{
		list_type source{allocator_type(first)};
		list_type target{allocator_type(second)};
		for (int i = 0; i < 10000; ++i) {
			source.push_back(i);
		}
		target.push_front(-1);
		target = source;
		require(target.size() == source.size() && target[9999] == 9999,
			"allocator copy assignment content mismatch");
		require(target.get_allocator().state == (Propagate ? first : second),
			"copy allocator propagation mismatch");
		list_type moved{allocator_type(second)};
		moved.push_back(-2);
		moved = std::move(source);
		require(moved.size() == 10000 && moved[9999] == 9999,
			"allocator move assignment content mismatch");
		require(moved.get_allocator().state == (Propagate ? first : second),
			"move allocator propagation mismatch");
		list_type same{allocator_type(moved.get_allocator())};
		same.push_back(42);
		same.swap(moved);
		require(moved.size() == 1 && moved.front() == 42 && same.size() == 10000,
			"equal allocator swap mismatch");
	}
	require(first->allocations.empty() && second->allocations.empty(),
		"allocator blocks leaked or ownership mismatch");
}

void test_allocation_failure_and_alignment() {
	auto state = std::make_shared<allocation_state>();
	using allocator_type = tracking_allocator<int, false>;
	{
		nova::list<int, allocator_type> values{allocator_type(state)};
		values.push_back(42);
		state->budget = 0;
		bool threw = false;
		try {
			values.reserve_back(100000);
		} catch (const std::bad_alloc&) {
			threw = true;
		}
		state->budget = -1;
		require(threw && values.size() == 1 && values.front() == 42,
			"failed reserve damaged existing elements");
		values.push_back(43);
		require(values.back() == 43, "list unusable after failed reserve");
	}
	require(state->allocations.empty(), "failed reserve leaked allocation");
	struct alignas(128) aligned_type { int value; std::array<std::byte, 124> padding{}; };
	nova::list<aligned_type> aligned;
	for (int i = 0; i < 1000; ++i) {
		aligned.emplace_front(aligned_type{i});
	}
	for (const auto& value : aligned) {
		require(reinterpret_cast<std::uintptr_t>(std::addressof(value)) % alignof(aligned_type) == 0,
			"over-aligned element address mismatch");
	}
}


template <std::size_t BlockBytes>
void test_tiny_blocks_and_sliding_queue() {
	using list_type = nova::list<int, std::allocator<int>, BlockBytes>;
	list_type values;
	std::deque<int> expected;
	values.reserve_front(3);
	values.reserve_back(3);
	for (int i = 0; i < 37; ++i) {
		values.emplace_back(i);
		expected.emplace_back(i);
	}
	for (int i = 37; i < 10000; ++i) {
		values.emplace_back(i);
		values.pop_front();
		expected.emplace_back(i);
		expected.pop_front();
		if (i % 101 == 0) {
			require_equal(values, expected);
		}
	}
	for (int i = 0; i < 10000; ++i) {
		values.emplace_front(-i);
		values.pop_back();
		expected.emplace_front(-i);
		expected.pop_back();
	}
	require_equal(values, expected);
	values.insert(values.begin() + 5, 11, 42);
	expected.insert(expected.begin() + 5, 11, 42);
	values.erase(values.begin() + 2, values.begin() + 13);
	expected.erase(expected.begin() + 2, expected.begin() + 13);
	require_equal(values, expected);
	values.reserve_front(100);
	values.reserve_back(100);
	values.optimize();
	require(values.is_contiguous(), "optimize did not produce contiguous storage");
	require_equal(values, expected);
	std::sort(values.begin(), values.end());
	std::sort(expected.begin(), expected.end());
	require_equal(values, expected);
	values.clear();
	values.optimize();
	require_equal(values, std::deque<int>{});
	values.emplace_front(1);
	values.emplace_back(2);
	require_equal(values, std::deque<int>{1, 2});
}

void test_self_ranges_and_input_iterators() {
	nova::list<int> values{0, 1, 2, 3, 4, 5};
	values.insert(values.begin() + 2, values.begin() + 1, values.end() - 1);
	require_equal(values, std::vector<int>{0, 1, 1, 2, 3, 4, 2, 3, 4, 5});
	values.assign(values.begin() + 2, values.end() - 2);
	require_equal(values, std::vector<int>{1, 2, 3, 4, 2, 3});
	std::istringstream constructor_input("1 2 3 4 5");
	nova::list<int> input_values{std::istream_iterator<int>(constructor_input), std::istream_iterator<int>()};
	require_equal(input_values, std::vector<int>{1, 2, 3, 4, 5});
	std::istringstream insert_input("10 11 12");
	input_values.insert(input_values.begin() + 2,
		std::istream_iterator<int>(insert_input), std::istream_iterator<int>());
	require_equal(input_values, std::vector<int>{1, 2, 10, 11, 12, 3, 4, 5});
	std::istringstream assign_input("7 8 9");
	input_values.assign(std::istream_iterator<int>(assign_input), std::istream_iterator<int>());
	require_equal(input_values, std::vector<int>{7, 8, 9});
}

void test_sized_range_insert_storage_modes() {
	for (bool optimized : {false, true}) {
		for (std::size_t position : {std::size_t{0}, std::size_t{4}, std::size_t{10}}) {
			nova::list<int, std::allocator<int>, 8> values;
			std::vector<int> expected;
			for (int i = 0; i < 10; ++i) {
				values.push_back(i);
				expected.push_back(i);
			}
			if (optimized) values.optimize();
			const std::vector<int> source(expected.begin() + 1, expected.end() - 1);
			values.insert(values.begin() + static_cast<std::ptrdiff_t>(position),
				values.begin() + 1, values.end() - 1);
			expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(position),
				source.begin(), source.end());
			require_equal(values, expected);
		}
	}
	for (bool front : {false, true}) {
		nova::list<std::unique_ptr<int>, std::allocator<std::unique_ptr<int>>, 32> values;
		values.emplace_back(std::make_unique<int>(-1));
		std::vector<std::unique_ptr<int>> source;
		for (int i = 0; i < 17; ++i) source.push_back(std::make_unique<int>(i));
		values.insert(front ? values.begin() : values.end(),
			std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
		require(values.size() == 18, "sized move range changed insertion size");
		for (std::size_t i = 0; i < source.size(); ++i) {
			require(source[i] == nullptr && *values[i + (front ? 0 : 1)] == static_cast<int>(i),
				"sized move range lost element ownership or order");
		}
		require(*(front ? values.back() : values.front()) == -1,
			"sized move range changed an existing element");
	}
}

void test_mixed_directory_recenter_ownership() {
	using allocator_type = tracking_allocator<int, false>;
	auto state = std::make_shared<allocation_state>();
	{
		nova::list<int, allocator_type, 8> values{allocator_type(state)};
		std::deque<int> expected;
		for (int i = 0; i < 17; ++i) {
			values.push_back(i);
			expected.push_back(i);
		}
		values.optimize();
		for (int pass = 0; pass < 2; ++pass) {
			for (bool front : {false, true}) {
				for (int step = 0; step < 500; ++step) {
					const int marker = 1000 + pass * 1000 + step;
					const int* anchor = &values[8];
					if (front) {
						values.push_front(marker);
						values.pop_back();
						expected.push_front(marker);
						expected.pop_back();
						require(&values[9] == anchor, "front recenter moved a surviving element");
					} else {
						values.push_back(marker);
						values.pop_front();
						expected.push_back(marker);
						expected.pop_front();
						require(&values[7] == anchor, "back recenter moved a surviving element");
					}
					if (step % 37 == 0) require_equal(values, expected);
				}
				require_equal(values, expected);
			}
			values.optimize();
		}
	}
	require(state->allocations.empty(), "mixed recenter leaked or duplicated block ownership");
}

void test_empty_reserve_failure() {
	using allocator_type = tracking_allocator<int, false>;
	for (bool front : {false, true}) {
		for (int budget : {0, 1, 2}) {
			auto state = std::make_shared<allocation_state>();
			{
				nova::list<int, allocator_type, 8> values{allocator_type(state)};
				state->budget = budget;
				bool threw = false;
				try {
					if (front) {
						values.reserve_front(100);
					} else {
						values.reserve_back(100);
					}
				} catch (const std::bad_alloc&) {
					threw = true;
				}
				state->budget = -1;
				require(threw && values.empty(), "failed empty reserve corrupted list");
				values.emplace_front(4);
				values.emplace_back(5);
				require(values.size() == 2 && values.front() == 4 && values.back() == 5,
					"failed empty reserve prevented reuse");
			}
			require(state->allocations.empty(), "failed empty reserve leaked blocks");
		}
	}
}


template <std::size_t BlockBytes>
void test_iterator_terminal_boundaries() {
	using list_type = nova::list<int, std::allocator<int>, BlockBytes>;
	using difference_type = typename list_type::difference_type;
	constexpr std::size_t width = list_type::k_block_capacity;
	typename list_type::iterator singular;
	typename list_type::const_iterator const_singular;
	require(singular == const_singular && !(singular != const_singular),
		"value-initialized iterator equality mismatch");
	require(singular + 0 == singular && singular - singular == 0,
		"value-initialized iterator zero arithmetic mismatch");
	const std::array counts{std::size_t{0}, std::size_t{1}, width - 1, width,
		width + 1, width * 2 - 1, width * 2, width * 2 + 1};
	const std::array prefixes{std::size_t{0}, std::size_t{1}, width - 1};
	for (bool reserve_next : {false, true}) {
		for (std::size_t prefix : prefixes) {
			for (std::size_t count : counts) {
				list_type values;
				std::vector<int> expected;
				for (std::size_t i = 0; i < prefix + count; ++i) {
					values.push_back(static_cast<int>(i));
				}
				for (std::size_t i = 0; i < prefix; ++i) {
					values.pop_front();
				}
				for (std::size_t i = 0; i < count; ++i) {
					expected.push_back(static_cast<int>(prefix + i));
				}
				if (reserve_next) {
					values.reserve_back(width * 3);
				}
				const list_type& constant = values;
				const auto first = values.begin();
				const auto finish = values.end();
				const auto const_finish = constant.end();
				require(finish == const_finish && !(finish != const_finish),
					"terminal mixed iterator equality mismatch");
				require(finish - first == static_cast<difference_type>(count),
					"terminal iterator distance mismatch");
				require(first + static_cast<difference_type>(count) == finish,
					"positive jump did not normalize terminal iterator");
				auto from_finish = finish;
				from_finish += -static_cast<difference_type>(count);
				require(from_finish == first, "negative jump from terminal iterator failed");
				require_equal(values, expected);

				auto cursor = first;
				std::size_t index = 0;
				while (cursor != const_finish) {
					require(index < count && *cursor == expected[index],
						"increment crossed logical end");
					const auto before = cursor++;
					require(*before == expected[index], "postincrement result mismatch");
					--cursor;
					require(cursor == before, "decrement after increment failed");
					++cursor;
					++index;
				}
				require(index == count && cursor == finish,
					"forward iteration did not reach logical terminal");
				while (index > 0) {
					const auto after = cursor--;
					--index;
					require(*cursor == expected[index], "reverse terminal traversal mismatch");
					require(cursor + 1 == after, "postdecrement result mismatch");
				}
				require(cursor == first, "reverse traversal did not reach first");
				for (std::size_t i = 0; i <= count; ++i) {
					const difference_type offset = static_cast<difference_type>(i);
					auto position = finish;
					position += offset - static_cast<difference_type>(count);
					const auto const_position = constant.begin() + offset;
					require(position == const_position && !(position != const_position),
						"mixed iterator jump equality mismatch");
					require(position - constant.begin() == offset,
						"mixed iterator jump distance mismatch");
					require(const_finish - position == static_cast<difference_type>(count) - offset,
						"mixed terminal distance mismatch");
					require((position <=> const_finish) == (offset <=> static_cast<difference_type>(count)),
						"terminal iterator ordering mismatch");
					if (i < count) {
						require(*position == expected[i] && *const_position == expected[i],
							"terminal relative jump addressed wrong element");
					}
				}
			}
		}
	}
}

template <std::size_t BlockBytes>
void test_iterator_subrange_algorithms() {
	using list_type = nova::list<int, std::allocator<int>, BlockBytes>;
	using difference_type = typename list_type::difference_type;
	constexpr std::size_t width = list_type::k_block_capacity;
	const std::size_t count = width * 3 + 7;
	list_type values;
	std::vector<int> expected;
	for (std::size_t i = 0; i < count + 1; ++i) {
		values.push_back(static_cast<int>((count + 1 - i) % 97));
	}
	values.pop_front();
	for (int value : values) {
		expected.push_back(value);
	}
	values.reserve_back(width * 2);
	std::vector<std::size_t> cuts{0, 1, width, width + 1, width * 2, count - 1, count};
	std::sort(cuts.begin(), cuts.end());
	cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
	for (std::size_t left : cuts) {
		for (std::size_t right : cuts) {
			if (left > right) {
				continue;
			}
			const auto first = values.begin() + static_cast<difference_type>(left);
			const auto last = values.begin() + static_cast<difference_type>(right);
			const auto expected_first = expected.begin() + static_cast<difference_type>(left);
			const auto expected_last = expected.begin() + static_cast<difference_type>(right);
			std::reverse(first, last);
			std::reverse(expected_first, expected_last);
			require_equal(values, expected);
			const std::size_t middle = left + (right - left) / 2;
			const auto rotated = std::rotate(first,
				values.begin() + static_cast<difference_type>(middle), last);
			const auto expected_rotated = std::rotate(expected_first,
				expected.begin() + static_cast<difference_type>(middle), expected_last);
			require(rotated - values.begin() == expected_rotated - expected.begin(),
				"subrange rotate returned wrong iterator");
			require_equal(values, expected);
			std::sort(first, last, std::greater<>{});
			std::sort(expected_first, expected_last, std::greater<>{});
			require_equal(values, expected);
		}
	}
}


struct move_only_visit_state {
	std::unique_ptr<long long> sum = std::make_unique<long long>(0);
	std::vector<int> visited;
	move_only_visit_state() = default;
	move_only_visit_state(const move_only_visit_state&) = delete;
	move_only_visit_state& operator=(const move_only_visit_state&) = delete;
	move_only_visit_state(move_only_visit_state&&) noexcept = default;
	move_only_visit_state& operator=(move_only_visit_state&&) noexcept = default;
	void operator()(const int& value) {
		*sum += value;
		visited.push_back(value);
	}
};

void test_member_for_each() {
	using list_type = nova::list<int, std::allocator<int>, 16>;
	static_assert(!std::is_copy_constructible_v<move_only_visit_state>);
	list_type values;
	values.reserve_front(20);
	values.reserve_back(120);
	for (int i = 0; i < 99; ++i) values.push_back(i);
	values.pop_front();
	values.pop_front();
	std::vector<int> expected(97);
	std::iota(expected.begin(), expected.end(), 2);
	const list_type& constant = values;
	auto state = constant.for_each(move_only_visit_state{});
	require(state.visited == expected, "const for_each order or callback state mismatch");
	require(*state.sum == std::accumulate(expected.begin(), expected.end(), 0LL),
		"const for_each accumulated value mismatch");
	values.for_each([](int& value) { value *= 3; });
	for (int& value : expected) value *= 3;
	require_equal(values, expected);
	auto alias_state = values.for_each(move_only_visit_state{});
	require(alias_state.visited == expected, "mutable for_each alias mismatch");
	auto const_alias_state = constant.for_each(move_only_visit_state{});
	require(const_alias_state.visited == expected, "const for_each alias mismatch");
	move_only_visit_state referenced;
	auto returned_reference = values.for_each(std::ref(referenced));
	require(std::addressof(returned_reference.get()) == std::addressof(referenced)
		&& referenced.visited == expected, "for_each reference_wrapper mismatch");

	int calls = 0;
	bool threw = false;
	try {
		values.for_each([&calls](int& value) {
			if (++calls == 17) throw std::runtime_error("callback failure");
			++value;
		});
	} catch (const std::runtime_error&) {
		threw = true;
	}
	require(threw && calls == 17, "for_each did not stop immediately on callback failure");
	for (std::size_t i = 0; i < 16; ++i) ++expected[i];
	require_equal(values, expected);
	auto after_failure = values.for_each(move_only_visit_state{});
	require(after_failure.visited == expected, "for_each failed to recover after callback exception");

	values.clear();
	values.reserve_back(40);
	auto empty_state = values.for_each(move_only_visit_state{});
	require(empty_state.visited.empty() && *empty_state.sum == 0,
		"for_each invoked callback for allocated empty list");
	list_type unallocated;
	auto unallocated_state = std::as_const(unallocated).for_each(move_only_visit_state{});
	require(unallocated_state.visited.empty(), "for_each invoked callback for default list");

	struct invocable_record {
		int value;
		mutable int visits = 0;
		void increment() { ++value; }
		int inspect() const { ++visits; return value; }
	};
	nova::list<invocable_record> records{{1}, {2}, {3}};
	records.for_each(&invocable_record::increment);
	std::as_const(records).for_each(&invocable_record::inspect);
	for (std::size_t i = 0; i < records.size(); ++i) {
		require(records[i].value == static_cast<int>(i) + 2 && records[i].visits == 1,
			"for_each did not use std::invoke member semantics");
	}
}

}  // namespace

int main() {
	try {
		test_basic_and_iterators();
		test_boundaries_and_stable_addresses();
		test_insert_erase_and_ranges();
		test_copy_move_and_sort();
		test_random_differential();
		test_move_only_and_lifetime();
		test_aliasing_and_throwing_moves();
		test_count_insert_aliasing_and_copy_failure();
		test_throwing_move_basic_guarantee();
		test_throwing_range_insert();
		test_allocator_propagation<false>();
		test_allocator_propagation<true>();
		test_allocation_failure_and_alignment();
		test_tiny_blocks_and_sliding_queue<4>();
		test_tiny_blocks_and_sliding_queue<8>();
		test_tiny_blocks_and_sliding_queue<16>();
		test_self_ranges_and_input_iterators();
		test_sized_range_insert_storage_modes();
		test_mixed_directory_recenter_ownership();
		test_empty_reserve_failure();
		test_iterator_terminal_boundaries<4>();
		test_iterator_terminal_boundaries<8>();
		test_iterator_terminal_boundaries<16>();
		test_iterator_terminal_boundaries<16384>();
		test_iterator_subrange_algorithms<4>();
		test_iterator_subrange_algorithms<8>();
		test_iterator_subrange_algorithms<16>();
		test_iterator_subrange_algorithms<16384>();
		test_member_for_each();
		std::cout << "All list tests passed.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "list_type test failure: " << error.what() << '\n';
		return 1;
	}
}
