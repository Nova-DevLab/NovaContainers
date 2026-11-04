#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "NovaContainers/nova_containers.hpp"

namespace {

void require(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}

template <typename list_type>
void require_sequence(const list_type& values, const std::vector<int>& expected) {
	require(values.size() == expected.size(), "optimized list size mismatch");
	require(std::equal(values.begin(), values.end(), expected.begin(), expected.end()),
		"optimized list forward sequence mismatch");
	require(std::equal(values.rbegin(), values.rend(), expected.rbegin(), expected.rend()),
		"optimized list reverse sequence mismatch");
	require(values.end() - values.begin() == static_cast<std::ptrdiff_t>(expected.size()),
		"optimized list iterator distance mismatch");
	std::size_t visited = 0;
	for (auto block : values.blocks()) {
		require(!block.empty(), "blocks exposed an empty optimized segment");
		for (int value : block) {
			require(visited < expected.size() && value == expected[visited++],
				"optimized block sequence mismatch");
		}
	}
	require(visited == expected.size(), "optimized block span count mismatch");
	for (std::size_t i = 0; i < values.size(); ++i) {
		require(values[i] == expected[i], "optimized random access mismatch");
	}
	if (values.is_contiguous()) {
		const auto view = values.contiguous_view();
		require(view.size() == expected.size(), "contiguous view size mismatch");
		for (std::size_t i = 0; i < view.size(); ++i) {
			require(std::addressof(values[i]) == view.data() + i && view[i] == expected[i],
				"contiguous view does not describe actual element layout");
		}
	} else {
		bool threw = false;
		try {
			(void)values.contiguous_view();
		} catch (const std::logic_error&) {
			threw = true;
		}
		require(threw, "const contiguous_view accepted a mixed allocation range");
	}
}

template <typename list_type>
void require_mutable_view_rejected(list_type& values) {
	bool threw = false;
	try {
		(void)values.contiguous_view();
	} catch (const std::logic_error&) {
		threw = true;
	}
	require(threw, "mutable contiguous_view accepted a mixed allocation range");
}

template <typename list_type>
concept has_rvalue_contiguous_view = requires(list_type&& values) {
	std::move(values).contiguous_view();
};

template <typename list_type>
concept has_const_rvalue_contiguous_view = requires(const list_type&& values) {
	std::move(values).contiguous_view();
};

template <std::size_t BlockBytes>
void test_optimize_boundaries() {
	using list_type = nova::list<int, std::allocator<int>, BlockBytes>;
	static_assert(std::is_same_v<decltype(std::declval<list_type&>().contiguous_view()), std::span<int>>);
	static_assert(std::is_same_v<decltype(std::declval<const list_type&>().contiguous_view()), std::span<const int>>);
	static_assert(!has_rvalue_contiguous_view<list_type>);
	static_assert(!has_const_rvalue_contiguous_view<list_type>);
	constexpr std::size_t width = list_type::k_block_capacity;
	const std::array counts{std::size_t{0}, std::size_t{1}, width - 1, width,
		width + 1, width * 2, width * 2 + 1};
	for (std::size_t prefix : {std::size_t{0}, std::size_t{1}, width - 1}) {
		for (std::size_t count : counts) {
			list_type values;
			std::vector<int> expected(count);
			std::iota(expected.begin(), expected.end(), 10);
			for (std::size_t i = 0; i < prefix; ++i) values.push_back(-1);
			for (int value : expected) values.push_back(value);
			for (std::size_t i = 0; i < prefix; ++i) values.pop_front();
			values.optimize();
			require(values.is_contiguous(), "optimize did not produce a single live allocation");
			require_sequence(values, expected);
			values.optimize();
			require(values.is_contiguous(), "repeated optimize lost contiguous layout");
			require_sequence(values, expected);
			values.reserve_front(width + 3);
			values.reserve_back(width + 5);
			require(values.is_contiguous(), "reserve changed live allocation contiguity");
			require_sequence(values, expected);
			values.optimize();
			require(values.is_contiguous(), "optimize lost contiguous layout");
			require_sequence(values, expected);
			if (!expected.empty()) {
				auto cursor = values.end();
				for (std::size_t i = expected.size(); i > 0; --i) {
					require(*--cursor == expected[i - 1], "contiguous end decrement mismatch");
				}
				require(cursor == values.begin(), "contiguous reverse traversal terminal mismatch");
				require(*(values.end() - 1) == expected.back(), "contiguous iterator negative jump mismatch");
			}
		}
	}
}

void test_empty_and_single_block_contiguity() {
	using list_type = nova::list<int, std::allocator<int>, 16>;
	list_type values;
	require(values.is_contiguous() && values.contiguous_view().empty(),
		"default empty list is not contiguous");
	values.reserve_front(50);
	values.reserve_back(50);
	require(values.is_contiguous() && values.contiguous_view().empty(),
		"reserved empty list is not contiguous");
	values.push_back(42);
	require(values.is_contiguous(), "single live element is not contiguous");
	require_sequence(values, {42});
	values.optimize();
	values.contiguous_view()[0] = 43;
	require_sequence(values, {43});
	values.clear();
	require(values.is_contiguous(), "cleared list is not contiguous");
	values.optimize();
	values.optimize();
	require_sequence(values, {});
	values.push_front(1);
	values.push_back(2);
	values.optimize();
	require_sequence(values, {1, 2});
}

void test_mixed_growth_and_reoptimization() {
	using list_type = nova::list<int, std::allocator<int>, 16>;
	list_type values;
	std::vector<int> expected(127);
	std::iota(expected.begin(), expected.end(), 0);
	values.assign(expected.begin(), expected.end());
	values.optimize();
	int* anchor = std::addressof(values[63]);
	for (int i = 0; i < 1500; ++i) {
		values.push_back(10000 + i);
		expected.push_back(10000 + i);
		values.push_front(-i - 1);
		expected.insert(expected.begin(), -i - 1);
	}
	require(std::addressof(values[1563]) == anchor && *anchor == 63,
		"mixed end growth moved an existing main-allocation element");
	require(!values.is_contiguous(), "growth beyond main allocation was marked contiguous");
	require_mutable_view_rejected(values);
	require_sequence(values, expected);
	for (int i = 0; i < 1500; ++i) {
		values.pop_front();
		values.pop_back();
	}
	expected.erase(expected.begin(), expected.begin() + 1500);
	expected.resize(expected.size() - 1500);
	require(values.is_contiguous(), "removing overflow blocks did not restore contiguous state");
	require(std::addressof(values[63]) == anchor,
		"draining overflow blocks moved a main-allocation element");
	require_sequence(values, expected);
	for (int pass = 0; pass < 4; ++pass) {
		for (int i = 0; i < 300; ++i) {
			values.push_front(-1000 - i);
			expected.insert(expected.begin(), -1000 - i);
			values.push_back(2000 + i);
			expected.push_back(2000 + i);
		}
		require_sequence(values, expected);
		values.optimize();
		require(values.is_contiguous(), "reoptimization failed to merge mixed storage");
		require_sequence(values, expected);
		values.for_each([](int& value) { ++value; });
		for (int& value : expected) ++value;
		require_sequence(values, expected);
	}
	std::sort(values.begin(), values.end());
	std::sort(expected.begin(), expected.end());
	require_sequence(values, expected);
	std::reverse(values.begin() + 3, values.end() - 5);
	std::reverse(expected.begin() + 3, expected.end() - 5);
	require_sequence(values, expected);
	values.insert(values.begin() + 11, 10, 42);
	expected.insert(expected.begin() + 11, 10, 42);
	values.erase(values.begin() + 2, values.begin() + 90);
	expected.erase(expected.begin() + 2, expected.begin() + 90);
	require_sequence(values, expected);
	values.optimize();
	require_sequence(values, expected);
}

void test_copy_move_swap_and_differential() {
	using list_type = nova::list<int, std::allocator<int>, 16>;
	list_type values;
	std::vector<int> expected;
	std::mt19937 random(0x4F505449);
	for (int step = 0; step < 7000; ++step) {
		const int value = static_cast<int>(random() % 10000);
		const std::size_t position = expected.empty() ? 0 : random() % (expected.size() + 1);
		switch (random() % 12) {
			case 0:
				values.optimize();
				require(values.is_contiguous(), "random optimize did not produce contiguous storage");
				break;
			case 1:
			case 2:
				values.push_front(value);
				expected.insert(expected.begin(), value);
				break;
			case 3:
			case 4:
				values.push_back(value);
				expected.push_back(value);
				break;
			case 5:
				values.insert(values.begin() + static_cast<std::ptrdiff_t>(position), value);
				expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(position), value);
				break;
			case 6:
				if (position < expected.size()) {
					values.erase(values.begin() + static_cast<std::ptrdiff_t>(position));
					expected.erase(expected.begin() + static_cast<std::ptrdiff_t>(position));
				}
				break;
			case 7:
				if (!expected.empty()) {
					values.pop_front();
					expected.erase(expected.begin());
				}
				break;
			case 8:
				if (!expected.empty()) {
					values.pop_back();
					expected.pop_back();
				}
				break;
			case 9:
				values.reserve_front(random() % 30);
				values.reserve_back(random() % 30);
				break;
			case 10:
				values.optimize();
				break;
			default: {
				list_type copied(values);
				require_sequence(copied, expected);
				list_type moved(std::move(copied));
				require_sequence(moved, expected);
				copied.push_back(9);
				copied = moved;
				require_sequence(copied, expected);
				list_type swapped{1, 2};
				swapped.swap(moved);
				require_sequence(swapped, expected);
				values = std::move(swapped);
				require_sequence(values, expected);
				break;
			}
		}
		if (step % 29 == 0) require_sequence(values, expected);
	}
	require_sequence(values, expected);
	values.optimize();
	require(values.is_contiguous(), "final differential optimize did not produce contiguous storage");
	require_sequence(values, expected);
}

struct copy_throw_value {
	inline static int live = 0;
	inline static int copy_budget = -1;
	int value;
	explicit copy_throw_value(int initial = 0) : value(initial) { ++live; }
	copy_throw_value(const copy_throw_value& other) : value(other.value) {
		if (copy_budget == 0) throw std::runtime_error("requested copy failure");
		if (copy_budget > 0) --copy_budget;
		++live;
	}
	copy_throw_value(copy_throw_value&& other) noexcept(false) : value(std::exchange(other.value, -1)) { ++live; }
	copy_throw_value& operator=(const copy_throw_value&) = default;
	copy_throw_value& operator=(copy_throw_value&& other) noexcept(false) {
		value = std::exchange(other.value, -1);
		return *this;
	}
	~copy_throw_value() { --live; }
};

struct move_throw_value {
	inline static int live = 0;
	inline static int move_budget = -1;
	int value;
	explicit move_throw_value(int initial = 0) : value(initial) { ++live; }
	move_throw_value(const move_throw_value&) = delete;
	move_throw_value& operator=(const move_throw_value&) = delete;
	move_throw_value(move_throw_value&& other) noexcept(false) : value(other.value) {
		if (move_budget == 0) throw std::runtime_error("requested move failure");
		if (move_budget > 0) --move_budget;
		other.value = -1;
		++live;
	}
	move_throw_value& operator=(move_throw_value&& other) noexcept {
		value = std::exchange(other.value, -1);
		return *this;
	}
	~move_throw_value() { --live; }
};

void test_optimize_exception_guarantees() {
	for (bool contiguous : {false, true}) {
		for (int budget : {0, 1, 9, 38}) {
			{
				nova::list<copy_throw_value, std::allocator<copy_throw_value>, 16> values;
				std::vector<copy_throw_value*> addresses;
				for (int i = 0; i < (contiguous ? 128 : 39); ++i) values.emplace_back(i);
				if (contiguous) {
					values.optimize();
					while (values.size() > 39) values.pop_back();
				}
				for (auto& value : values) addresses.push_back(std::addressof(value));
				copy_throw_value::copy_budget = budget;
				bool threw = false;
				try {
					values.optimize();
				} catch (const std::runtime_error&) {
					threw = true;
				}
				copy_throw_value::copy_budget = -1;
				require(threw && values.size() == 39, "throwing copy optimize did not preserve size");
				require(copy_throw_value::live == 39, "throwing copy optimize leaked live objects");
				for (std::size_t i = 0; i < values.size(); ++i) {
					require(values[i].value == static_cast<int>(i)
						&& std::addressof(values[i]) == addresses[i],
						"throwing copy optimize violated strong guarantee");
				}
				values.optimize();
				require(values.is_contiguous(), "optimize failed after copy exception recovery");
			}
			require(copy_throw_value::live == 0, "copy exception cleanup leaked objects");
		}
	}
	for (int budget : {0, 1, 9, 38}) {
		{
			nova::list<move_throw_value, std::allocator<move_throw_value>, 16> values;
			for (int i = 0; i < 39; ++i) values.emplace_back(i);
			move_throw_value::move_budget = budget;
			bool threw = false;
			try {
				values.optimize();
			} catch (const std::runtime_error&) {
				threw = true;
			}
			move_throw_value::move_budget = -1;
			require(threw && move_throw_value::live == static_cast<int>(values.size()),
				"throwing move optimize violated lifetime guarantee");
			std::size_t count = 0;
			for ([[maybe_unused]] const auto& value : values) ++count;
			require(count == values.size(), "throwing move optimize corrupted iteration");
			values.clear();
			require(move_throw_value::live == 0, "throwing move optimize leaked moved values");
			values.emplace_front(42);
			values.optimize();
			require(values.front().value == 42 && values.is_contiguous(),
				"list unusable after throwing move optimize");
		}
		require(move_throw_value::live == 0, "move exception cleanup leaked objects");
	}
}

struct allocation_state {
	std::map<void*, std::size_t> allocations;
	std::size_t constructed = 0;
	std::size_t destroyed = 0;
	int budget = -1;
};

template <typename T, bool Propagate>
struct checked_allocator {
	using value_type = T;
	using propagate_on_container_copy_assignment = std::bool_constant<Propagate>;
	using propagate_on_container_move_assignment = std::bool_constant<Propagate>;
	using propagate_on_container_swap = std::bool_constant<Propagate>;
	using is_always_equal = std::false_type;
	template <typename U> struct rebind { using other = checked_allocator<U, Propagate>; };
	std::shared_ptr<allocation_state> state = std::make_shared<allocation_state>();
	checked_allocator() = default;
	explicit checked_allocator(std::shared_ptr<allocation_state> initial) : state(std::move(initial)) {}
	template <typename U>
	checked_allocator(const checked_allocator<U, Propagate>& other) noexcept : state(other.state) {}
	T* allocate(std::size_t count) {
		if (state->budget == 0) throw std::bad_alloc();
		if (state->budget > 0) --state->budget;
		T* result = std::allocator<T>{}.allocate(count);
		state->allocations.emplace(result, count);
		return result;
	}
	void deallocate(T* pointer, std::size_t count) noexcept {
		const auto found = state->allocations.find(pointer);
		if (found == state->allocations.end() || found->second != count) std::terminate();
		state->allocations.erase(found);
		std::allocator<T>{}.deallocate(pointer, count);
	}
	template <typename U, typename... Args>
	void construct(U* pointer, Args&&... args) {
		std::construct_at(pointer, std::forward<Args>(args)...);
		++state->constructed;
	}
	template <typename U>
	void destroy(U* pointer) noexcept {
		std::destroy_at(pointer);
		++state->destroyed;
	}
	template <typename U>
	bool operator==(const checked_allocator<U, Propagate>& other) const noexcept {
		return state == other.state;
	}
};

template <std::size_t BlockBytes>
void test_main_capacity_reuse() {
	using allocator_type = checked_allocator<int, false>;
	using list_type = nova::list<int, allocator_type, BlockBytes>;
	constexpr std::size_t width = list_type::k_block_capacity;
	auto state = std::make_shared<allocation_state>();
	{
		list_type values{allocator_type(state)};
		const std::size_t count = width * 3 + 1;
		for (std::size_t i = 0; i < count; ++i) values.push_back(static_cast<int>(i));
		values.optimize();
		int* base = values.contiguous_view().data();
		const std::size_t capacity = values.capacity();
		require(capacity == width * 4, "unexpected rounded main capacity");
		state->budget = 0;
		for (std::size_t i = 0; i < width + 1; ++i) values.pop_front();
		for (std::size_t i = 0; i < width; ++i) values.pop_back();
		require(values.capacity() == capacity, "trimming discarded reusable main capacity");
		require(values.front_capacity() == width + 1
			&& values.back_capacity() == width * 2 - 1,
			"trimmed main directional capacity mismatch");
		for (std::size_t i = width + 1; i > 0; --i) values.push_front(static_cast<int>(i - 1));
		while (values.size() < capacity) values.push_back(static_cast<int>(values.size()));
		require(values.contiguous_view().data() == base && state->allocations.size() == 1,
			"refill did not reuse the original main allocation");
		for (std::size_t i = 0; i < capacity; ++i)
			require(values[i] == static_cast<int>(i), "main refill changed element order");
		while (!values.empty()) values.pop_front();
		require(values.capacity() == capacity, "emptying discarded main capacity");
		const std::size_t front_slots = values.front_capacity();
		const std::size_t back_slots = values.back_capacity();
		require(front_slots + back_slots == capacity, "empty main capacity partition mismatch");
		values.reserve_front(front_slots);
		values.reserve_back(back_slots);
		for (std::size_t i = 0; i < front_slots; ++i) values.push_front(1);
		for (std::size_t i = 0; i < back_slots; ++i) values.push_back(2);
		require(values.is_contiguous() && values.contiguous_view().data() == base,
			"empty main allocation could not be reused in both directions");
		values.clear();
		require(values.capacity() == capacity, "clear discarded main capacity");
		for (std::size_t i = 0; i < capacity; ++i) values.push_back(static_cast<int>(i));
		state->budget = -1;
		values.push_front(-1);
		values.push_back(static_cast<int>(capacity));
		require(!values.is_contiguous(), "overflow blocks unexpectedly contiguous");
		values.pop_front();
		values.pop_back();
		require(values.capacity() == capacity && state->allocations.size() == 1,
			"unused overflow blocks were retained or main slices were discarded");
		require(values.contiguous_view().data() == base, "overflow removal lost the main allocation");
		values.push_back(static_cast<int>(capacity));
		for (std::size_t i = 0; i < capacity; ++i) values.pop_front();
		require(values.capacity() == width && state->allocations.size() == 1,
			"completely unused main allocation was not released");
		state->budget = 0;
		while (values.back_capacity() > 0) values.push_back(3);
		require(values.capacity() == values.front_capacity() + values.size() + values.back_capacity(),
			"mixed storage capacity partition mismatch");
		state->budget = -1;
		values.clear();
		values.optimize();
		require(values.capacity() == 0 && state->allocations.empty(),
			"empty optimize did not release retained capacity");
	}

	for (bool overflow_front : {false, true}) {
		list_type values{allocator_type(state)};
		for (std::size_t i = 0; i < width * 3; ++i) values.push_back(static_cast<int>(i));
		values.optimize();
		if (overflow_front) values.push_front(-1);
		else values.push_back(-1);
		for (std::size_t i = 0; i < width * 3; ++i) {
			if (overflow_front) values.pop_back();
			else values.pop_front();
		}
		require(values.size() == 1 && values.front() == -1
			&& values.capacity() == width && state->allocations.size() == 1,
			"unused main allocation was not released in both directions");
		for (std::size_t i = 0; i < width * 20; ++i) {
			if (overflow_front) { values.push_front(-1); values.pop_back(); }
			else { values.push_back(-1); values.pop_front(); }
			require(values.capacity() <= width * 2,
				"sliding queue retained obsolete blocks after leaving the main allocation");
		}
	}
	require(state->allocations.empty() && state->constructed == state->destroyed,
		"main capacity reuse leaked storage or live objects");
}

void test_optimize_reclaims_storage() {
	using allocator_type = checked_allocator<int, false>;
	using list_type = nova::list<int, allocator_type, 16>;
	auto state = std::make_shared<allocation_state>();
	{
		list_type values{allocator_type(state)};
		for (int i = 0; i < 4096; ++i) values.push_back(i);
		values.optimize();
		const std::size_t before_bytes = state->allocations.begin()->second * sizeof(int);
		for (int i = 0; i < 1000; ++i) values.pop_front();
		while (values.size() > 5) values.pop_back();
		require(values.is_contiguous(), "trimmed main allocation lost contiguity");
		int* original = std::addressof(values[0]);
		state->budget = 0;
		bool threw = false;
		try { values.optimize(); } catch (const std::bad_alloc&) { threw = true; }
		state->budget = -1;
		require(threw && std::addressof(values[0]) == original,
			"failed contiguous shrink changed element addresses");
		require_sequence(values, {1000, 1001, 1002, 1003, 1004});
		values.optimize();
		std::size_t after_bytes = 0;
		for (const auto& allocation : state->allocations) after_bytes += allocation.second * sizeof(int);
		std::cout << "Retained element storage: " << before_bytes << " -> " << after_bytes << " bytes\n";
		require(state->allocations.size() == 1 && after_bytes == 8 * sizeof(int),
			"optimize retained an oversized contiguous main allocation");
		require_sequence(values, {1000, 1001, 1002, 1003, 1004});
		int* anchor = std::addressof(values[0]);
		state->budget = 0;
		values.optimize();
		require(std::addressof(values[0]) == anchor, "minimal optimize allocated or moved elements");
		state->budget = -1;
		values.reserve_front(100);
		values.reserve_back(100);
		values.optimize();
		require(state->allocations.size() == 1 && state->allocations.begin()->second == 8,
			"optimize retained reserved blocks around contiguous elements");
		require_sequence(values, {1000, 1001, 1002, 1003, 1004});
		values.clear();
		values.optimize();
		require(state->allocations.empty() && values.capacity() == 0,
			"empty optimize retained storage");
		for (int i = 0; i < 128; ++i) values.push_back(i);
		values.optimize();
		values.push_back(128);
		for (int i = 0; i < 128; ++i) values.pop_front();
		require(values.is_contiguous(), "single overflow element is not contiguous");
		values.optimize();
		require(state->allocations.size() == 1 && state->allocations.begin()->second == 4,
			"optimize retained an unused main allocation behind a live owned block");
		require_sequence(values, {128});
		values.reserve_front(80);
		values.reserve_back(80);
		values.optimize();
		require(state->allocations.size() == 1 && state->allocations.begin()->second == 4,
			"single-block optimize retained reservations");
	}
	require(state->allocations.empty() && state->constructed == state->destroyed,
		"storage reclamation leaked allocations or live objects");
}

template <bool Propagate>
void test_allocator_ownership() {
	using allocator_type = checked_allocator<int, Propagate>;
	using list_type = nova::list<int, allocator_type, 16>;
	auto first = std::make_shared<allocation_state>();
	auto second = std::make_shared<allocation_state>();
	{
		list_type source{allocator_type(first)};
		std::vector<int> expected(117);
		std::iota(expected.begin(), expected.end(), 0);
		source.assign(expected.begin(), expected.end());
		source.optimize();
		require(first->constructed - first->destroyed == source.size(),
			"optimize did not balance allocator construction hooks");
		for (int i = 0; i < 100; ++i) source.push_back(500 + i);
		for (int i = 0; i < 100; ++i) source.pop_back();
		require_sequence(source, expected);
		list_type target{allocator_type(second)};
		target.push_front(-1);
		target = source;
		require_sequence(target, expected);
		require(target.get_allocator().state == (Propagate ? first : second),
			"optimized copy allocator propagation mismatch");
		target.optimize();
		list_type moved{allocator_type(second)};
		moved.push_back(-2);
		moved = std::move(source);
		require_sequence(moved, expected);
		require(moved.get_allocator().state == (Propagate ? first : second),
			"optimized move allocator propagation mismatch");
		moved.optimize();
		list_type equal{moved.get_allocator()};
		equal.push_back(42);
		equal.swap(moved);
		require_sequence(equal, expected);
		require_sequence(moved, {42});
		list_type explicit_copy(equal, allocator_type(second));
		require_sequence(explicit_copy, expected);
		explicit_copy.optimize();
		list_type explicit_move(std::move(equal), allocator_type(first));
		require_sequence(explicit_move, expected);
		explicit_move.optimize();
	}
	require(first->allocations.empty() && second->allocations.empty(),
		"optimized allocator ownership leaked or deallocated a main slice");
	require(first->constructed == first->destroyed && second->constructed == second->destroyed,
		"optimized allocator construct/destroy hooks are unbalanced");
}

void test_allocation_failure_and_move_only() {
	using allocator_type = checked_allocator<int, false>;
	auto state = std::make_shared<allocation_state>();
	{
		nova::list<int, allocator_type, 16> values{allocator_type(state)};
		std::vector<int> expected(75);
		std::iota(expected.begin(), expected.end(), 1);
		values.assign(expected.begin(), expected.end());
		int* anchor = std::addressof(values[20]);
		state->budget = 0;
		bool threw = false;
		try {
			values.optimize();
		} catch (const std::bad_alloc&) {
			threw = true;
		}
		state->budget = -1;
		require(threw && std::addressof(values[20]) == anchor,
			"failed optimize allocation changed existing addresses");
		require_sequence(values, expected);
		values.optimize();
		require_sequence(values, expected);
	}
	require(state->allocations.empty() && state->constructed == state->destroyed,
		"optimize allocation failure leaked owned storage");

	nova::list<std::unique_ptr<int>, std::allocator<std::unique_ptr<int>>, 32> pointers;
	for (int i = 0; i < 97; ++i) pointers.emplace_back(std::make_unique<int>(i));
	pointers.optimize();
	require(pointers.is_contiguous(), "move-only optimize failed to produce contiguous storage");
	for (std::size_t i = 0; i < pointers.size(); ++i) {
		require(*pointers[i] == static_cast<int>(i), "move-only optimize lost a value");
	}
	pointers.emplace_front(std::make_unique<int>(-1));
	pointers.emplace_back(std::make_unique<int>(99));
	pointers.optimize();
	std::sort(pointers.begin(), pointers.end(),
		[](const auto& left, const auto& right) { return *left > *right; });
	require(*pointers.front() == 99 && *pointers.back() == -1,
		"optimized move-only sort failed");
}


void test_mutable_view_and_iterator_arithmetic() {
	using list_type = nova::list<int, std::allocator<int>, 16>;
	list_type values;
	std::vector<int> expected(37);
	std::iota(expected.begin(), expected.end(), 0);
	for (int value : expected) values.push_back(value);
	for (int phase = 0; phase < 3; ++phase) {
		values.optimize();
		auto view = values.contiguous_view();
		for (std::size_t i = 0; i < view.size(); ++i) {
			view[i] = expected[i] * 2 + 1;
			expected[i] = view[i];
		}
		require_sequence(values, expected);
		for (std::size_t i = 0; i <= values.size(); ++i) {
			auto position = values.begin() + static_cast<std::ptrdiff_t>(i);
			list_type::const_iterator const_position = position;
			require(const_position - values.cbegin() == static_cast<std::ptrdiff_t>(i),
				"const iterator conversion lost optimized position");
			require(values.end() - position == static_cast<std::ptrdiff_t>(values.size() - i),
				"optimized iterator arithmetic lost distance");
			position -= static_cast<std::ptrdiff_t>(i);
			require(position == values.begin(), "optimized iterator subtraction lost begin");
		}
		values.push_front(-phase - 1);
		expected.insert(expected.begin(), -phase - 1);
		values.push_back(phase + 100);
		expected.push_back(phase + 100);
		require_mutable_view_rejected(values);
		require_sequence(values, expected);
	}
}

void test_aligned_non_default_values() {
	struct alignas(128) aligned_value_type {
		int value;
		std::array<std::byte, 124> padding{};
		aligned_value_type() = delete;
		explicit aligned_value_type(int initial) : value(initial) {}
	};
	nova::list<aligned_value_type, std::allocator<aligned_value_type>, 256> values;
	for (int i = 0; i < 47; ++i) values.emplace_back(i);
	values.optimize();
	require(values.is_contiguous(), "over-aligned optimize did not produce contiguous storage");
	for (std::size_t i = 0; i < values.size(); ++i) {
		require(values[i].value == static_cast<int>(i)
			&& reinterpret_cast<std::uintptr_t>(std::addressof(values[i])) % alignof(aligned_value_type) == 0,
			"optimize lost alignment or non-default-constructed value");
	}
	values.emplace_front(-1);
	values.emplace_back(47);
	values.optimize();
	require(values.front().value == -1 && values.back().value == 47,
		"over-aligned mixed reoptimization failed");
}

}  // namespace

int main() {
	try {
		test_empty_and_single_block_contiguity();
		test_optimize_reclaims_storage();
		test_main_capacity_reuse<4>();
		test_main_capacity_reuse<16>();
		test_main_capacity_reuse<16384>();
		test_optimize_boundaries<4>();
		test_optimize_boundaries<8>();
		test_optimize_boundaries<16>();
		test_optimize_boundaries<16384>();
		test_mixed_growth_and_reoptimization();
		test_copy_move_swap_and_differential();
		test_optimize_exception_guarantees();
		test_allocator_ownership<false>();
		test_allocator_ownership<true>();
		test_allocation_failure_and_move_only();
		test_mutable_view_and_iterator_arithmetic();
		test_aligned_non_default_values();
		std::cout << "All list optimize tests passed.\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "list_type optimize test failure: " << error.what() << '\n';
		return 1;
	}
}
