#ifndef NOVA_CONTAINERS_DETAIL_FLAT_TABLE_HPP_
#define NOVA_CONTAINERS_DETAIL_FLAT_TABLE_HPP_

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "NovaContainers/detail/concepts.hpp"

namespace nova::containers::detail {

template <typename StoredValue, typename Key, typename KeyOfValue,
		  typename Compare, typename Alloc>
	requires KeyOrderedWith<Compare, Key, Key>
class FlatTable {
 public:
	using value_type = StoredValue;
	using key_type = Key;
	using key_compare = Compare;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using allocator_traits = std::allocator_traits<allocator_type>;
	using storage_type = std::vector<value_type, allocator_type>;
	using iterator = typename storage_type::iterator;
	using const_iterator = typename storage_type::const_iterator;

	static_assert(std::same_as<typename allocator_traits::value_type, value_type>,
		"FlatTable allocator value_type must match StoredValue");

	FlatTable() = default;

	explicit FlatTable(const Compare& compare, const Alloc& allocator = Alloc{})
		: values_(allocator), compare_(compare) {}

	FlatTable(size_type initial_capacity, const Compare& compare = Compare{},
		const Alloc& allocator = Alloc{})
		: values_(allocator), compare_(compare) {
		Reserve(initial_capacity);
	}

	FlatTable(const FlatTable&) = default;
	FlatTable(FlatTable&&) = default;
	FlatTable& operator=(const FlatTable&) = default;
	FlatTable& operator=(FlatTable&&) = default;
	~FlatTable() = default;

	[[nodiscard]] size_type Size() const noexcept { return values_.size(); }
	[[nodiscard]] size_type Capacity() const noexcept {
		return values_.capacity();
	}
	[[nodiscard]] bool Empty() const noexcept { return values_.empty(); }
	[[nodiscard]] allocator_type GetAllocator() const {
		return values_.get_allocator();
	}
	[[nodiscard]] key_compare KeyComp() const { return compare_; }

	void Swap(FlatTable& other) noexcept(
		std::is_nothrow_swappable_v<Compare> &&
		(allocator_traits::propagate_on_container_swap::value
			 ? std::is_nothrow_swappable_v<Alloc>
			 : allocator_traits::is_always_equal::value)) {
		if constexpr (!allocator_traits::propagate_on_container_swap::value &&
			!allocator_traits::is_always_equal::value) {
			if (values_.get_allocator() != other.values_.get_allocator()) {
				throw std::invalid_argument(
					"FlatTable::Swap: incompatible allocators");
			}
		}

		using std::swap;
		swap(compare_, other.compare_);
		values_.swap(other.values_);
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	iterator LowerBound(const LookupKey& key) {
		return std::lower_bound(values_.begin(), values_.end(), key,
			[this](const value_type& value, const LookupKey& lookup_key) {
				return compare_(KeyFrom(value), lookup_key);
			});
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	const_iterator LowerBound(const LookupKey& key) const {
		return std::lower_bound(values_.begin(), values_.end(), key,
			[this](const value_type& value, const LookupKey& lookup_key) {
				return compare_(KeyFrom(value), lookup_key);
			});
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	iterator UpperBound(const LookupKey& key) {
		return std::upper_bound(values_.begin(), values_.end(), key,
			[this](const LookupKey& lookup_key, const value_type& value) {
				return compare_(lookup_key, KeyFrom(value));
			});
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	const_iterator UpperBound(const LookupKey& key) const {
		return std::upper_bound(values_.begin(), values_.end(), key,
			[this](const LookupKey& lookup_key, const value_type& value) {
				return compare_(lookup_key, KeyFrom(value));
			});
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	iterator Find(const LookupKey& key) {
		const iterator position = LowerBound(key);
		return position != values_.end() && Equivalent(KeyFrom(*position), key)
			? position
			: values_.end();
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	const_iterator Find(const LookupKey& key) const {
		const const_iterator position = LowerBound(key);
		return position != values_.end() && Equivalent(KeyFrom(*position), key)
			? position
			: values_.end();
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	[[nodiscard]] bool Contains(const LookupKey& key) const {
		return Find(key) != values_.end();
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	std::pair<iterator, iterator> EqualRange(const LookupKey& key) {
		const iterator first = LowerBound(key);
		if (first == values_.end() || !Equivalent(KeyFrom(*first), key)) {
			return {first, first};
		}
		return {first, first + 1};
	}

	template <typename LookupKey>
		requires KeyOrderedWith<Compare, Key, LookupKey>
	std::pair<const_iterator, const_iterator> EqualRange(
		const LookupKey& key) const {
		const const_iterator first = LowerBound(key);
		if (first == values_.end() || !Equivalent(KeyFrom(*first), key)) {
			return {first, first};
		}
		return {first, first + 1};
	}

	template <typename V>
	std::pair<iterator, bool> Insert(V&& value) {
		const Key& key = KeyFrom(value);
		const iterator position = LowerBound(key);
		if (position != values_.end() && Equivalent(KeyFrom(*position), key)) {
			return {position, false};
		}
		return {values_.insert(position, std::forward<V>(value)), true};
	}

	template <typename LookupKey, typename Factory>
		requires KeyOrderedWith<Compare, Key, LookupKey> &&
			std::invocable<Factory> &&
			std::constructible_from<value_type, std::invoke_result_t<Factory>>
	std::pair<iterator, bool> InsertWithKey(
		const LookupKey& key, Factory&& factory) {
		const iterator position = LowerBound(key);
		if (position != values_.end() && Equivalent(KeyFrom(*position), key)) {
			return {position, false};
		}
		return {values_.emplace(position,
			std::invoke(std::forward<Factory>(factory))), true};
	}

	iterator Erase(iterator position) { return values_.erase(position); }
	iterator Erase(const_iterator position) { return values_.erase(position); }

	void Clear() noexcept { values_.clear(); }
	void Reserve(size_type element_count) { values_.reserve(element_count); }
	void ShrinkToFit() { values_.shrink_to_fit(); }

	template <typename InputIt>
	void AssignUnsorted(InputIt first, InputIt last) {
		storage_type replacement(values_.get_allocator());
		replacement.assign(first, last);
		std::sort(replacement.begin(), replacement.end(),
			[this](const value_type& left, const value_type& right) {
				return compare_(KeyFrom(left), KeyFrom(right));
			});
		ValidateUnique(replacement);
		values_.swap(replacement);
	}

	template <typename InputIt>
	void AssignSortedUnique(InputIt first, InputIt last) {
		storage_type replacement(values_.get_allocator());
		replacement.assign(first, last);
		ValidateStrictOrder(replacement);
		values_.swap(replacement);
	}

	iterator Begin() noexcept { return values_.begin(); }
	iterator End() noexcept { return values_.end(); }
	const_iterator Begin() const noexcept { return values_.begin(); }
	const_iterator End() const noexcept { return values_.end(); }
	const_iterator CBegin() const noexcept { return values_.cbegin(); }
	const_iterator CEnd() const noexcept { return values_.cend(); }

 private:
	static const Key& KeyFrom(const value_type& value) noexcept {
		return KeyOfValue{}(value);
	}

	template <typename Left, typename Right>
	bool Equivalent(const Left& left, const Right& right) const {
		return !compare_(left, right) && !compare_(right, left);
	}

	void ValidateUnique(const storage_type& values) const {
		for (size_type index = 1; index < values.size(); ++index) {
			if (Equivalent(KeyFrom(values[index - 1]), KeyFrom(values[index]))) {
				throw std::invalid_argument(
					"FlatTable::AssignUnsorted: duplicate key");
			}
		}
	}

	void ValidateStrictOrder(const storage_type& values) const {
		for (size_type index = 1; index < values.size(); ++index) {
			if (!compare_(KeyFrom(values[index - 1]), KeyFrom(values[index]))) {
				throw std::invalid_argument(
					"FlatTable::AssignSortedUnique: input is not strictly ordered");
			}
		}
	}

	storage_type values_;
	[[no_unique_address]] Compare compare_{};
};

}  // namespace nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_FLAT_TABLE_HPP_
