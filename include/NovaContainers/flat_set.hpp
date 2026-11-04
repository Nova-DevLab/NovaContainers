#ifndef NOVA_CONTAINERS_FLAT_SET_HPP_
#define NOVA_CONTAINERS_FLAT_SET_HPP_

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/flat/flat_table.hpp"
#include "NovaContainers/detail/key_extractors.hpp"

namespace nova::containers {

template <typename Key, typename Compare = std::less<>,
		  typename Alloc = std::allocator<Key>>
	requires detail::key_ordered_with<Compare, Key, Key>
class flat_set {
 public:
	using key_type = Key;
	using value_type = Key;
	using key_compare = Compare;
	using value_compare = Compare;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = const value_type&;
	using const_reference = const value_type&;
	using pointer = typename std::allocator_traits<Alloc>::const_pointer;
	using const_pointer = typename std::allocator_traits<Alloc>::const_pointer;

 private:
	using core_type = detail::flat_table<value_type, Key,
		detail::identity_key_of_value, Compare, Alloc>;

 public:
	using iterator = typename core_type::const_iterator;
	using const_iterator = typename core_type::const_iterator;

	flat_set() = default;

	explicit flat_set(const Compare& compare, const Alloc& allocator = Alloc{})
		: core_(compare, allocator) {}

	flat_set(size_type initial_capacity, const Compare& compare = Compare{},
		const Alloc& allocator = Alloc{})
		: core_(initial_capacity, compare, allocator) {}

	flat_set(std::initializer_list<value_type> init,
		const Compare& compare = Compare{}, const Alloc& allocator = Alloc{})
		: core_(init.size(), compare, allocator) {
		for (const value_type& value : init) {
			insert(value);
		}
	}

	flat_set(const flat_set&) = default;
	flat_set(flat_set&&) = default;
	flat_set& operator=(const flat_set&) = default;
	flat_set& operator=(flat_set&&) = default;
	~flat_set() = default;

	[[nodiscard]] size_type size() const noexcept { return core_.size(); }
	[[nodiscard]] size_type capacity() const noexcept { return core_.capacity(); }
	[[nodiscard]] bool empty() const noexcept { return core_.empty(); }
	[[nodiscard]] allocator_type get_allocator() const {
		return core_.get_allocator();
	}
	[[nodiscard]] key_compare key_comp() const { return core_.key_comp(); }
	[[nodiscard]] value_compare value_comp() const { return core_.key_comp(); }

	[[nodiscard]] size_type count(const Key& key) const {
		return contains(key) ? 1 : 0;
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	[[nodiscard]] size_type count(const LookupKey& key) const {
		return contains(key) ? 1 : 0;
	}

	void swap(flat_set& other) noexcept(noexcept(core_.swap(other.core_))) {
		core_.swap(other.core_);
	}

	iterator find(const Key& key) const { return core_.find(key); }

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	iterator find(const LookupKey& key) const {
		return core_.find(key);
	}

	[[nodiscard]] bool contains(const Key& key) const {
		return core_.contains(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	[[nodiscard]] bool contains(const LookupKey& key) const {
		return core_.contains(key);
	}

	template <typename... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		value_type value(std::forward<Args>(args)...);
		return convert_insert_result(core_.insert(std::move(value)));
	}

	std::pair<iterator, bool> insert(const value_type& value) {
		return convert_insert_result(core_.insert(value));
	}

	std::pair<iterator, bool> insert(value_type&& value) {
		return convert_insert_result(core_.insert(std::move(value)));
	}

	bool erase(const Key& key) {
		return core_.erase_key(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	bool erase(const LookupKey& key) {
		return core_.erase_key(key);
	}

	iterator erase(iterator position) { return core_.erase(position); }
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

	iterator lower_bound(const Key& key) const { return core_.lower_bound(key); }
	iterator upper_bound(const Key& key) const { return core_.upper_bound(key); }

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	iterator lower_bound(const LookupKey& key) const {
		return core_.lower_bound(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	iterator upper_bound(const LookupKey& key) const {
		return core_.upper_bound(key);
	}

	std::pair<iterator, iterator> equal_range(const Key& key) const {
		return core_.equal_range(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_ordered_lookup<Compare, Key, LookupKey>
	std::pair<iterator, iterator> equal_range(const LookupKey& key) const {
		return core_.equal_range(key);
	}

	iterator begin() const noexcept { return core_.cbegin(); }
	iterator end() const noexcept { return core_.cend(); }

	iterator begin() noexcept { return core_.cbegin(); }
	iterator end() noexcept { return core_.cend(); }
	const_iterator cbegin() const noexcept { return const_iterator(core_.cbegin()); }
	const_iterator cend() const noexcept { return const_iterator(core_.cend()); }

 private:
	static std::pair<iterator, bool> convert_insert_result(
		std::pair<typename core_type::iterator, bool> result) {
		return {iterator(result.first), result.second};
	}

	core_type core_;
};

}  // 네임스페이스 nova::containers

namespace nova {

template <typename Key, typename Compare = std::less<>,
		  typename Alloc = std::allocator<Key>>
using flat_set = containers::flat_set<Key, Compare, Alloc>;

}  // 네임스페이스 nova

#endif  // NOVA_CONTAINERS_FLAT_SET_HPP_
