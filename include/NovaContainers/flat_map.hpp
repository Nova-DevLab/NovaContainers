#ifndef NOVA_CONTAINERS_FLAT_MAP_HPP_
#define NOVA_CONTAINERS_FLAT_MAP_HPP_

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/flat/flat_table.hpp"
#include "NovaContainers/detail/flat/map_iterator.hpp"
#include "NovaContainers/detail/key_extractors.hpp"

namespace nova::containers {

template <typename Key, typename Value, typename Compare = std::less<>,
		  typename Alloc = std::allocator<std::pair<Key, Value>>>
	requires detail::key_ordered_with<Compare, Key, Key>
class flat_map {
 public:
	using key_type = Key;
	using mapped_type = Value;
	using value_type = std::pair<Key, Value>;
	using key_compare = Compare;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

 private:
	using core_type = detail::flat_table<value_type, Key,
		detail::pair_first_key_of_value, Compare, Alloc>;

 public:
	template <bool IsConst>
	using basic_iterator = detail::flat_map_iterator<core_type, flat_map, IsConst>;

	using iterator = basic_iterator<false>;
	using const_iterator = basic_iterator<true>;
	using reference = typename iterator::reference;
	using const_reference = typename const_iterator::reference;
	using pointer = typename iterator::pointer;
	using const_pointer = typename const_iterator::pointer;

	flat_map() = default;

	explicit flat_map(const Compare& compare, const Alloc& allocator = Alloc{})
		: core_(compare, allocator) {}

	flat_map(size_type initial_capacity, const Compare& compare = Compare{},
		const Alloc& allocator = Alloc{})
		: core_(initial_capacity, compare, allocator) {}

	flat_map(std::initializer_list<value_type> init,
		const Compare& compare = Compare{}, const Alloc& allocator = Alloc{})
		: core_(init.size(), compare, allocator) {
		for (const value_type& value : init) {
			insert(value);
		}
	}

	flat_map(const flat_map&) = default;
	flat_map(flat_map&&) = default;
	flat_map& operator=(const flat_map&) = default;
	flat_map& operator=(flat_map&&) = default;
	~flat_map() = default;

	[[nodiscard]] size_type size() const noexcept { return core_.size(); }
	[[nodiscard]] size_type capacity() const noexcept { return core_.capacity(); }
	[[nodiscard]] bool empty() const noexcept { return core_.empty(); }
	[[nodiscard]] allocator_type get_allocator() const {
		return core_.get_allocator();
	}
	[[nodiscard]] key_compare key_comp() const { return core_.key_comp(); }

	[[nodiscard]] size_type count(const Key& key) const {
		return contains(key) ? 1 : 0;
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	[[nodiscard]] size_type count(const LookupKey& key) const {
		return contains(key) ? 1 : 0;
	}

	void swap(flat_map& other) noexcept(noexcept(core_.swap(other.core_))) {
		core_.swap(other.core_);
	}

	iterator find(const Key& key) { return iterator(core_.find(key)); }
	const_iterator find(const Key& key) const {
		return const_iterator(core_.find(key));
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	iterator find(const LookupKey& key) {
		return iterator(core_.find(key));
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	const_iterator find(const LookupKey& key) const {
		return const_iterator(core_.find(key));
	}

	[[nodiscard]] bool contains(const Key& key) const {
		return core_.contains(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	[[nodiscard]] bool contains(const LookupKey& key) const {
		return core_.contains(key);
	}

	Value& at(const Key& key) {
		const iterator position = find(key);
		if (position == end()) {
			throw std::out_of_range("nova::flat_map::at: key not found");
		}
		return position->second;
	}

	const Value& at(const Key& key) const {
		const const_iterator position = find(key);
		if (position == end()) {
			throw std::out_of_range("nova::flat_map::at: key not found");
		}
		return position->second;
	}

	template <typename... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		value_type value(std::forward<Args>(args)...);
		return convert_insert_result(core_.insert(std::move(value)));
	}

	template <typename... Args>
	std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
		return convert_insert_result(core_.insert_with_key(key, [&]() {
			return value_type(std::piecewise_construct,
				std::forward_as_tuple(key),
				std::forward_as_tuple(std::forward<Args>(args)...));
		}));
	}

	template <typename... Args>
	std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
		return convert_insert_result(core_.insert_with_key(key, [&]() {
			return value_type(std::piecewise_construct,
				std::forward_as_tuple(std::move(key)),
				std::forward_as_tuple(std::forward<Args>(args)...));
		}));
	}

	std::pair<iterator, bool> insert(const value_type& value) {
		return convert_insert_result(core_.insert(value));
	}

	std::pair<iterator, bool> insert(value_type&& value) {
		return convert_insert_result(core_.insert(std::move(value)));
	}

	template <typename M>
	std::pair<iterator, bool> insert_or_assign(const Key& key, M&& value) {
		auto result = core_.insert_with_key(key, [&]() {
			return value_type(key, std::forward<M>(value));
		});
		if (!result.second) {
			result.first->second = std::forward<M>(value);
		}
		return convert_insert_result(std::move(result));
	}

	template <typename M>
	std::pair<iterator, bool> insert_or_assign(Key&& key, M&& value) {
		auto result = core_.insert_with_key(key, [&]() {
			return value_type(std::move(key), std::forward<M>(value));
		});
		if (!result.second) {
			result.first->second = std::forward<M>(value);
		}
		return convert_insert_result(std::move(result));
	}

	bool erase(const Key& key) {
		return core_.erase_key(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	bool erase(const LookupKey& key) {
		return core_.erase_key(key);
	}

	iterator erase(iterator position) {
		return iterator(core_.erase(position.iterator_));
	}

	iterator erase(const_iterator position) {
		return iterator(core_.erase(position.iterator_));
	}

	void clear() noexcept { core_.clear(); }
	void reserve(size_type element_count) { core_.reserve(element_count); }
	void shrink_to_fit() { core_.shrink_to_fit(); }

	template <typename InputIt>
	void assign_unsorted(InputIt first, InputIt last) {
		core_.assign_unsorted(first, last);
	}

	template <typename InputIt>
	void assign_sorted_unique(InputIt first, InputIt last) {
		core_.assign_sorted_unique(first, last);
	}

	iterator lower_bound(const Key& key) {
		return iterator(core_.lower_bound(key));
	}
	const_iterator lower_bound(const Key& key) const {
		return const_iterator(core_.lower_bound(key));
	}
	iterator upper_bound(const Key& key) {
		return iterator(core_.upper_bound(key));
	}
	const_iterator upper_bound(const Key& key) const {
		return const_iterator(core_.upper_bound(key));
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	iterator lower_bound(const LookupKey& key) {
		return iterator(core_.lower_bound(key));
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	const_iterator lower_bound(const LookupKey& key) const {
		return const_iterator(core_.lower_bound(key));
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	iterator upper_bound(const LookupKey& key) {
		return iterator(core_.upper_bound(key));
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	const_iterator upper_bound(const LookupKey& key) const {
		return const_iterator(core_.upper_bound(key));
	}

	std::pair<iterator, iterator> equal_range(const Key& key) {
		const auto [first, last] = core_.equal_range(key);
		return {iterator(first), iterator(last)};
	}

	std::pair<const_iterator, const_iterator> equal_range(const Key& key) const {
		const auto [first, last] = core_.equal_range(key);
		return {const_iterator(first), const_iterator(last)};
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	std::pair<iterator, iterator> equal_range(const LookupKey& key) {
		const auto [first, last] = core_.equal_range(key);
		return {iterator(first), iterator(last)};
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	std::pair<const_iterator, const_iterator> equal_range(
		const LookupKey& key) const {
		const auto [first, last] = core_.equal_range(key);
		return {const_iterator(first), const_iterator(last)};
	}

	iterator begin() noexcept { return iterator(core_.begin()); }
	iterator end() noexcept { return iterator(core_.end()); }
	const_iterator begin() const noexcept {
		return const_iterator(core_.begin());
	}
	const_iterator end() const noexcept { return const_iterator(core_.end()); }

	const_iterator cbegin() const noexcept { return const_iterator(core_.cbegin()); }
	const_iterator cend() const noexcept { return const_iterator(core_.cend()); }

	Value& operator[](const Key& key) {
		return try_emplace(key).first->second;
	}
	Value& operator[](Key&& key) {
		return try_emplace(std::move(key)).first->second;
	}

 private:
	static std::pair<iterator, bool> convert_insert_result(
		std::pair<typename core_type::iterator, bool> result) {
		return {iterator(result.first), result.second};
	}

	core_type core_;
};

}  // 네임스페이스 nova::containers

namespace nova {

template <typename Key, typename Value, typename Compare = std::less<>,
		  typename Alloc = std::allocator<std::pair<Key, Value>>>
using flat_map = containers::flat_map<Key, Value, Compare, Alloc>;

}  // 네임스페이스 nova

#endif  // NOVA_CONTAINERS_FLAT_MAP_HPP_
