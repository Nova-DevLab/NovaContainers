#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
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

struct throw_on_construction {
	explicit throw_on_construction(int initial_value) : value(initial_value) {
		if (should_throw) {
			throw std::runtime_error("construction failure");
		}
	}

	throw_on_construction(const throw_on_construction&) = default;
	throw_on_construction(throw_on_construction&&) noexcept = default;
	throw_on_construction& operator=(const throw_on_construction&) = default;
	throw_on_construction& operator=(throw_on_construction&&) noexcept = default;

	inline static bool should_throw = false;
	int value;
};

struct direction_compare {
	bool operator()(int left, int right) const noexcept {
		return descending ? left > right : left < right;
	}

	bool descending = false;
};

struct allocation_state {
	std::size_t allocated_bytes = 0;
	std::size_t deallocated_bytes = 0;
};

template <typename T, typename TrackedValue = T>
class tracking_allocator {
 public:
	using value_type = T;

	template <typename U>
	struct rebind {
		using other = tracking_allocator<U, TrackedValue>;
	};

	tracking_allocator() : state_(std::make_shared<allocation_state>()) {}
	explicit tracking_allocator(std::shared_ptr<allocation_state> state)
		: state_(std::move(state)) {}

	template <typename U>
	tracking_allocator(const tracking_allocator<U, TrackedValue>& other) noexcept
		: state_(other.state()) {}

	T* allocate(std::size_t count) {
		if constexpr (std::is_same_v<T, TrackedValue>) {
			state_->allocated_bytes += count * sizeof(T);
		}
		return std::allocator<T>{}.allocate(count);
	}

	void deallocate(T* pointer, std::size_t count) noexcept {
		if constexpr (std::is_same_v<T, TrackedValue>) {
			state_->deallocated_bytes += count * sizeof(T);
		}
		std::allocator<T>{}.deallocate(pointer, count);
	}

	[[nodiscard]] const std::shared_ptr<allocation_state>& state() const noexcept {
		return state_;
	}

	template <typename U>
	bool operator==(
		const tracking_allocator<U, TrackedValue>& other) const noexcept {
		return state_ == other.state();
	}

 private:
	template <typename, typename>
	friend class tracking_allocator;

	std::shared_ptr<allocation_state> state_;
};

void test_flat_map_basic_operations() {
	using map_type = nova::flat_map<std::string, int>;
	using map_reference = decltype(*std::declval<map_type::iterator>());
	static_assert(std::is_const_v<std::remove_reference_t<
		decltype(std::declval<map_reference>().first)>>);
	static_assert(!std::is_const_v<std::remove_reference_t<
		decltype(std::declval<map_reference>().second)>>);

	map_type values{{"gamma", 3}, {"alpha", 1}, {"beta", 2}};
	require(values.size() == 3, "flat_map initializer size mismatch");
	require(values.begin()->first == "alpha", "flat_map is not key ordered");
	require(values.begin()[1].first == "beta",
		"flat_map random-access iterator mismatch");
	require(values.end() - values.begin() == 3,
		"flat_map iterator distance mismatch");
	const map_type::value_type first_copy = *values.begin();
	require(first_copy.first == "alpha" && first_copy.second == 1,
		"flat_map iterator proxy did not copy to value_type");

	std::string concatenated_keys;
	for (auto&& [key, value] : values) {
		concatenated_keys += key;
		value *= 10;
	}
	require(concatenated_keys == "alphabetagamma",
		"flat_map iteration order mismatch");
	require(values.at("beta") == 20,
		"flat_map iterator mapped mutation failed");

	const auto [duplicate, duplicate_inserted] = values.insert({"beta", 200});
	require(!duplicate_inserted && duplicate->second == 20,
		"flat_map duplicate insert changed mapped value");
	const auto [inserted, was_inserted] = values.try_emplace("delta", 4);
	require(was_inserted && inserted->second == 4,
		"flat_map try_emplace failed");
	values.insert_or_assign("alpha", 100);
	require(values["alpha"] == 100, "flat_map insert_or_assign failed");
	require(values["epsilon"] == 0, "flat_map operator[] failed");

	require(values.lower_bound("beta")->first == "beta",
		"flat_map lower_bound mismatch");
	require(values.upper_bound("beta")->first == "delta",
		"flat_map upper_bound mismatch");
	const auto [equal_first, equal_last] = values.equal_range("beta");
	require(equal_last - equal_first == 1 && equal_first->first == "beta",
		"flat_map equal_range mismatch");
	require(values.erase("gamma"), "flat_map erase(key) failed");
	require(!values.contains("gamma"), "flat_map erased key remains");

	bool threw = false;
	try {
		(void)values.at("missing");
	} catch (const std::out_of_range&) {
		threw = true;
	}
	require(threw, "flat_map at() did not throw for a missing key");
}

void test_flat_map_iterator_compatibility() {
	using map_type = nova::flat_map<int, int>;
	static_assert(std::is_same_v<map_type::basic_iterator<false>, map_type::iterator>);
	static_assert(std::is_same_v<map_type::basic_iterator<true>, map_type::const_iterator>);
	static_assert(std::is_constructible_v<map_type::const_iterator, map_type::iterator>);
	static_assert(!std::is_constructible_v<map_type::iterator, map_type::const_iterator>);

	map_type values{{1, 10}, {2, 20}, {3, 30}, {4, 40}};
	map_type::iterator first = values.begin();
	map_type::const_iterator const_first = first;
	require(first == const_first && const_first == first,
		"mutable and const iterators must compare equally");
	require(values.cend() - first == 4 && values.end() - const_first == 4,
		"mixed iterator distance must preserve storage positions");
	require(first < values.cend() && const_first < values.end(),
		"mixed iterator ordering must preserve storage positions");
	require((2 + first)->first == 3 && first[3].second == 40,
		"iterator offset and indexing must preserve proxy references");
	first[1].second = 25;
	require(values.at(2) == 25, "proxy reference must update the mapped value");

	const auto next = values.erase(const_first + 1);
	require(next->first == 3 && values.size() == 3,
		"const iterator erase must return the next element");
	require(values.erase(values.begin())->first == 3,
		"mutable iterator erase must return the next element");
}

void test_flat_map_deferred_construction() {
	nova::flat_map<int, throw_on_construction> values;
	values.reserve(8);
	values.try_emplace(1, 10);

	throw_on_construction::should_throw = true;
	const auto [duplicate, duplicate_inserted] = values.try_emplace(1, 20);
	require(!duplicate_inserted && duplicate->second.value == 10,
		"flat_map duplicate try_emplace constructed a mapped value");

	bool threw = false;
	try {
		values.try_emplace(2, 20);
	} catch (const std::runtime_error&) {
		threw = true;
	}
	require(threw, "flat_map did not propagate a mapped constructor failure");
	require(values.size() == 1 && !values.contains(2),
		"flat_map changed after a mapped constructor failure");

	throw_on_construction::should_throw = false;
	values.try_emplace(2, 20);
	require(values.at(2).value == 20,
		"flat_map failed after a mapped constructor exception");
}

void test_flat_map_bulk_assignment() {
	nova::flat_map<int, std::string> values{{9, "preserved"}};
	const std::vector<std::pair<int, std::string>> unsorted{
		{3, "three"}, {1, "one"}, {2, "two"}};
	values.assign_unsorted(unsorted.begin(), unsorted.end());
	require(values.size() == 3 && values.begin()->first == 1,
		"flat_map assign_unsorted did not sort input");
	require(values.at(3) == "three",
		"flat_map assign_unsorted lost mapped data");

	const std::vector<std::pair<int, std::string>> duplicate{
		{1, "first"}, {1, "second"}};
	bool duplicate_threw = false;
	try {
		values.assign_unsorted(duplicate.begin(), duplicate.end());
	} catch (const std::invalid_argument&) {
		duplicate_threw = true;
	}
	require(duplicate_threw, "flat_map bulk duplicate was not rejected");
	require(values.size() == 3 && values.at(2) == "two",
		"flat_map changed after rejected bulk duplicate");

	const std::vector<std::pair<int, std::string>> sorted{
		{4, "four"}, {5, "five"}, {6, "six"}};
	values.assign_sorted_unique(sorted.begin(), sorted.end());
	require(values.begin()->first == 4 && values.at(6) == "six",
		"flat_map assign_sorted_unique failed");

	const std::vector<std::pair<int, std::string>> out_of_order{
		{8, "eight"}, {7, "seven"}};
	bool order_threw = false;
	try {
		values.assign_sorted_unique(out_of_order.begin(), out_of_order.end());
	} catch (const std::invalid_argument&) {
		order_threw = true;
	}
	require(order_threw, "flat_map unordered sorted input was not rejected");
	require(values.size() == 3 && values.begin()->first == 4,
		"flat_map changed after rejected sorted input");
}

void test_flat_map_heterogeneous_lookup() {
	nova::flat_map<std::string, int> values{
		{"alpha", 1}, {"beta", 2}, {"gamma", 3}};
	const std::string_view beta = "beta";
	const std::string_view between = "delta";
	require(values.find(beta) != values.end() && values.find(beta)->second == 2,
		"flat_map heterogeneous find failed");
	require(values.lower_bound(between)->first == "gamma",
		"flat_map heterogeneous lower_bound failed");
	require(values.erase(beta), "flat_map heterogeneous erase failed");
	require(!values.contains(beta),
		"flat_map heterogeneous erased key remains");
}

void test_flat_set_operations() {
	using set_type = nova::flat_set<std::string>;
	static_assert(std::is_const_v<std::remove_reference_t<
		decltype(*std::declval<set_type::iterator>())>>);

	set_type values{"gamma", "alpha", "beta", "beta"};
	require(values.size() == 3, "flat_set duplicate initializer mismatch");
	require(*values.begin() == "alpha", "flat_set is not key ordered");
	require(values.end() - values.begin() == 3,
		"flat_set iterator distance mismatch");
	require(!values.insert("beta").second,
		"flat_set duplicate insertion succeeded");
	require(values.insert("delta").second,
		"flat_set insertion failed");
	require(values.lower_bound("beta") != values.end(),
		"flat_set lower_bound failed");
	const auto [equal_first, equal_last] = values.equal_range("beta");
	require(equal_last - equal_first == 1 && *equal_first == "beta",
		"flat_set equal_range failed");
	require(values.erase("gamma"), "flat_set erase failed");

	const std::vector<std::string> unsorted{"zeta", "eta", "theta"};
	values.assign_unsorted(unsorted.begin(), unsorted.end());
	require(*values.begin() == "eta" && values.contains("theta"),
		"flat_set assign_unsorted failed");

	const std::string_view theta = "theta";
	require(values.find(theta) != values.end(),
		"flat_set heterogeneous lookup failed");
}

void test_descending_comparator() {
	nova::flat_map<int, int, std::greater<>> values;
	values.insert({1, 10});
	values.insert({3, 30});
	values.insert({2, 20});
	require(values.begin()->first == 3,
		"flat_map did not preserve comparator order");
	require(values.lower_bound(2)->first == 2,
		"flat_map comparator lower_bound mismatch");

	nova::flat_set<int, std::greater<>> keys{1, 3, 2};
	require(*keys.begin() == 3,
		"flat_set did not preserve comparator order");
}

void test_stateful_comparator_copy_move_and_swap() {
	using map_type = nova::flat_map<int, int, direction_compare>;
	map_type descending(0, direction_compare{true});
	descending.insert({1, 10});
	descending.insert({3, 30});
	descending.insert({2, 20});

	map_type copied = descending;
	require(copied.begin()->first == 3 && copied.key_comp().descending,
		"flat_map copy lost stateful comparator state");
	map_type moved = std::move(copied);
	require(moved.begin()->first == 3 && moved.key_comp().descending,
		"flat_map move lost stateful comparator state");

	map_type ascending(0, direction_compare{false});
	ascending.insert({4, 40});
	ascending.insert({5, 50});
	moved.swap(ascending);
	require(moved.begin()->first == 4 && !moved.key_comp().descending,
		"flat_map swap lost ascending comparator state");
	require(ascending.begin()->first == 3 && ascending.key_comp().descending,
		"flat_map swap lost descending comparator state");
}

void test_differential_operations() {
	nova::flat_map<int, int> actual_map;
	std::map<int, int> expected_map;
	nova::flat_set<int> actual_set;
	std::set<int> expected_set;
	std::uint32_t state = 0xC001CAFEU;

	for (int operation = 0; operation < 20000; ++operation) {
		state = state * 1664525U + 1013904223U;
		const int key = static_cast<int>((state >> 8) % 1000U);
		const int value = operation * 3;
		switch (state & 3U) {
			case 0:
				actual_map.insert_or_assign(key, value);
				expected_map.insert_or_assign(key, value);
				actual_set.insert(key);
				expected_set.insert(key);
				break;
			case 1:
				require(actual_map.erase(key) == (expected_map.erase(key) != 0),
					"flat_map differential erase mismatch");
				require(actual_set.erase(key) == (expected_set.erase(key) != 0),
					"flat_set differential erase mismatch");
				break;
			default: {
				const auto actual = actual_map.find(key);
				const auto expected = expected_map.find(key);
				require((actual == actual_map.end()) ==
					(expected == expected_map.end()),
					"flat_map differential find mismatch");
				require(actual_set.contains(key) == expected_set.contains(key),
					"flat_set differential contains mismatch");
				break;
			}
		}
		require(actual_map.size() == expected_map.size(),
			"flat_map differential size mismatch");
		require(actual_set.size() == expected_set.size(),
			"flat_set differential size mismatch");
	}

	auto actual_map_it = actual_map.begin();
	for (const auto& [key, value] : expected_map) {
		require(actual_map_it != actual_map.end(),
			"flat_map differential iteration ended early");
		require(actual_map_it->first == key && actual_map_it->second == value,
			"flat_map differential ordered content mismatch");
		++actual_map_it;
	}
	require(actual_map_it == actual_map.end(),
		"flat_map differential iteration has extra elements");
	require(std::equal(actual_set.begin(), actual_set.end(),
		expected_set.begin(), expected_set.end()),
		"flat_set differential ordered content mismatch");
}

void test_direct_storage_accounting() {
	using pair_type = std::pair<int, int>;
	const std::shared_ptr<allocation_state> map_state =
		std::make_shared<allocation_state>();
	const std::shared_ptr<allocation_state> set_state =
		std::make_shared<allocation_state>();

	{
		using map_allocator = tracking_allocator<pair_type>;
		nova::flat_map<int, int, std::less<>, map_allocator> values(
			0, std::less<>{}, map_allocator(map_state));
		values.reserve(100000);
		require(map_state->allocated_bytes == values.capacity() * sizeof(pair_type),
			"flat_map allocation is not direct pair storage");
	}
	{
		using set_allocator = tracking_allocator<int>;
		nova::flat_set<int, std::less<>, set_allocator> values(
			0, std::less<>{}, set_allocator(set_state));
		values.reserve(100000);
		require(set_state->allocated_bytes == values.capacity() * sizeof(int),
			"flat_set allocation is not direct key storage");
	}

	require(map_state->allocated_bytes == map_state->deallocated_bytes,
		"flat_map allocation/deallocation mismatch");
	require(set_state->allocated_bytes == set_state->deallocated_bytes,
		"flat_set allocation/deallocation mismatch");
}

}  // namespace

int main() {
	try {
		test_flat_map_basic_operations();
		test_flat_map_iterator_compatibility();
		test_flat_map_deferred_construction();
		test_flat_map_bulk_assignment();
		test_flat_map_heterogeneous_lookup();
		test_flat_set_operations();
		test_descending_comparator();
		test_stateful_comparator_copy_move_and_swap();
		test_differential_operations();
		test_direct_storage_accounting();
		std::cout << "All flat container tests passed.\n";
		return 0;
	} catch (const std::exception& exception) {
		throw_on_construction::should_throw = false;
		std::cerr << "flat container test failure: " << exception.what() << '\n';
		return 1;
	}
}
