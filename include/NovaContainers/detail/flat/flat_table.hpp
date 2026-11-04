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
	requires key_ordered_with<Compare, Key, Key>
class flat_table {
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
		"flat_table allocator value_type must match StoredValue");

	flat_table() = default;

	explicit flat_table(const Compare& compare, const Alloc& allocator = Alloc{})
		: values_(allocator), compare_(compare) {}

	flat_table(size_type initial_capacity, const Compare& compare = Compare{},
		const Alloc& allocator = Alloc{})
		: values_(allocator), compare_(compare) {
		reserve(initial_capacity);
	}

	flat_table(const flat_table&) = default;
	flat_table(flat_table&&) = default;
	flat_table& operator=(const flat_table&) = default;
	flat_table& operator=(flat_table&&) = default;
	~flat_table() = default;

	[[nodiscard]] size_type size() const noexcept { return values_.size(); }
	[[nodiscard]] size_type capacity() const noexcept {
		return values_.capacity();
	}
	[[nodiscard]] bool empty() const noexcept { return values_.empty(); }
	[[nodiscard]] allocator_type get_allocator() const {
		return values_.get_allocator();
	}
	[[nodiscard]] key_compare key_comp() const { return compare_; }

	void swap(flat_table& other) noexcept(
		std::is_nothrow_swappable_v<Compare> &&
		(allocator_traits::propagate_on_container_swap::value
			 ? std::is_nothrow_swappable_v<Alloc>
			 : allocator_traits::is_always_equal::value)) {
		if constexpr (!allocator_traits::propagate_on_container_swap::value &&
			!allocator_traits::is_always_equal::value) {
			if (values_.get_allocator() != other.values_.get_allocator()) {
				throw std::invalid_argument(
					"flat_table::swap: incompatible allocators");
			}
		}

		using std::swap;
		swap(compare_, other.compare_);
		values_.swap(other.values_);
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	iterator lower_bound(const LookupKey& key) {
		return std::lower_bound(values_.begin(), values_.end(), key,
			[this](const value_type& value, const LookupKey& lookup_key) {
				return compare_(key_from(value), lookup_key);
			});
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	const_iterator lower_bound(const LookupKey& key) const {
		return std::lower_bound(values_.begin(), values_.end(), key,
			[this](const value_type& value, const LookupKey& lookup_key) {
				return compare_(key_from(value), lookup_key);
			});
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	iterator upper_bound(const LookupKey& key) {
		return std::upper_bound(values_.begin(), values_.end(), key,
			[this](const LookupKey& lookup_key, const value_type& value) {
				return compare_(lookup_key, key_from(value));
			});
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	const_iterator upper_bound(const LookupKey& key) const {
		return std::upper_bound(values_.begin(), values_.end(), key,
			[this](const LookupKey& lookup_key, const value_type& value) {
				return compare_(lookup_key, key_from(value));
			});
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	iterator find(const LookupKey& key) {
		const iterator position = lower_bound(key);
		return position != values_.end() && equivalent(key_from(*position), key)
			? position
			: values_.end();
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	const_iterator find(const LookupKey& key) const {
		const const_iterator position = lower_bound(key);
		return position != values_.end() && equivalent(key_from(*position), key)
			? position
			: values_.end();
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	[[nodiscard]] bool contains(const LookupKey& key) const {
		return find(key) != values_.end();
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	std::pair<iterator, iterator> equal_range(const LookupKey& key) {
		const iterator first = lower_bound(key);
		if (first == values_.end() || !equivalent(key_from(*first), key)) {
			return {first, first};
		}
		return {first, first + 1};
	}

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	std::pair<const_iterator, const_iterator> equal_range(
		const LookupKey& key) const {
		const const_iterator first = lower_bound(key);
		if (first == values_.end() || !equivalent(key_from(*first), key)) {
			return {first, first};
		}
		return {first, first + 1};
	}

	template <typename V>
	std::pair<iterator, bool> insert(V&& value) {
		const Key& key = key_from(value);
		const iterator position = lower_bound(key);
		if (position != values_.end() && equivalent(key_from(*position), key)) {
			return {position, false};
		}
		return {values_.insert(position, std::forward<V>(value)), true};
	}

	template <typename LookupKey, typename Factory>
		requires key_ordered_with<Compare, Key, LookupKey> &&
			std::invocable<Factory> &&
			std::constructible_from<value_type, std::invoke_result_t<Factory>>
	std::pair<iterator, bool> insert_with_key(
		const LookupKey& key, Factory&& factory) {
		const iterator position = lower_bound(key);
		if (position != values_.end() && equivalent(key_from(*position), key)) {
			return {position, false};
		}
		return {values_.emplace(position,
			std::invoke(std::forward<Factory>(factory))), true};
	}

	iterator erase(iterator position) { return values_.erase(position); }
	iterator erase(const_iterator position) { return values_.erase(position); }

	template <typename LookupKey>
		requires key_ordered_with<Compare, Key, LookupKey>
	bool erase_key(const LookupKey& key) {
		const iterator position = find(key);
		if (position == end()) {
			return false;
		}
		erase(position);
		return true;
	}

	void clear() noexcept { values_.clear(); }
	void reserve(size_type element_count) { values_.reserve(element_count); }
	void shrink_to_fit() { values_.shrink_to_fit(); }

	template <typename InputIt>
	void assign_unsorted(InputIt first, InputIt last) {
		storage_type replacement(values_.get_allocator());
		replacement.assign(first, last);
		std::sort(replacement.begin(), replacement.end(),
			[this](const value_type& left, const value_type& right) {
				return compare_(key_from(left), key_from(right));
			});
		validate_unique(replacement);
		values_.swap(replacement);
	}

	template <typename InputIt>
	void assign_sorted_unique(InputIt first, InputIt last) {
		storage_type replacement(values_.get_allocator());
		replacement.assign(first, last);
		validate_strict_order(replacement);
		values_.swap(replacement);
	}

	iterator begin() noexcept { return values_.begin(); }
	iterator end() noexcept { return values_.end(); }
	const_iterator begin() const noexcept { return values_.begin(); }
	const_iterator end() const noexcept { return values_.end(); }
	const_iterator cbegin() const noexcept { return values_.cbegin(); }
	const_iterator cend() const noexcept { return values_.cend(); }

 private:
	static const Key& key_from(const value_type& value) noexcept {
		return KeyOfValue{}(value);
	}

	template <typename Left, typename Right>
	bool equivalent(const Left& left, const Right& right) const {
		return !compare_(left, right) && !compare_(right, left);
	}

	void validate_unique(const storage_type& values) const {
		for (size_type index = 1; index < values.size(); ++index) {
			if (equivalent(key_from(values[index - 1]), key_from(values[index]))) {
				throw std::invalid_argument(
					"flat_table::assign_unsorted: duplicate key");
			}
		}
	}

	void validate_strict_order(const storage_type& values) const {
		for (size_type index = 1; index < values.size(); ++index) {
			if (!compare_(key_from(values[index - 1]), key_from(values[index]))) {
				throw std::invalid_argument(
					"flat_table::assign_sorted_unique: input is not strictly ordered");
			}
		}
	}

	storage_type values_;
	[[no_unique_address]] Compare compare_{};
};

}  // 네임스페이스 nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_FLAT_TABLE_HPP_
