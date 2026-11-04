#ifndef NOVA_CONTAINERS_HASH_SET_HPP_
#define NOVA_CONTAINERS_HASH_SET_HPP_

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/key_extractors.hpp"
#include "NovaContainers/detail/hash/raw_hash_table.hpp"

namespace nova::containers {

template <typename Key, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<Key>>
	requires detail::hashable<Hash, Key> && detail::key_equality<Key, Eq>
class hash_set {
 public:
	using key_type = Key;
	using value_type = Key;
	using hasher = Hash;
	using key_equal = Eq;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = const value_type&;
	using const_reference = const value_type&;
	using pointer = typename std::allocator_traits<Alloc>::const_pointer;
	using const_pointer = typename std::allocator_traits<Alloc>::const_pointer;

 private:
	using core_type = detail::raw_hash_table<value_type, Key,
		detail::identity_key_of_value, Hash, Eq, Alloc>;

 public:
	using iterator = typename core_type::const_iterator;
	using const_iterator = typename core_type::const_iterator;

	hash_set() = default;

	explicit hash_set(size_type initial_capacity, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: core_(initial_capacity, hash, equal, allocator) {}

	hash_set(std::initializer_list<value_type> init, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: core_(0, hash, equal, allocator) {
		reserve(init.size());
		for (const value_type& value : init) {
			insert(value);
		}
	}

	hash_set(const hash_set&) = default;
	hash_set(hash_set&&) = default;
	hash_set& operator=(const hash_set&) = default;
	hash_set& operator=(hash_set&&) = default;
	~hash_set() = default;

	[[nodiscard]] size_type size() const noexcept { return core_.size(); }
	[[nodiscard]] size_type capacity() const noexcept { return core_.capacity(); }
	[[nodiscard]] bool empty() const noexcept { return core_.empty(); }
	[[nodiscard]] float load_factor() const noexcept { return core_.load_factor(); }
	[[nodiscard]] constexpr float max_load_factor() const noexcept {
		return core_.max_load_factor();
	}
	[[nodiscard]] allocator_type get_allocator() const {
		return core_.get_allocator();
	}

	[[nodiscard]] size_type count(const Key& key) const {
		return contains(key) ? 1 : 0;
	}

	template <typename LookupKey>
		requires detail::heterogeneous_hash_lookup<Hash, Eq, Key, LookupKey>
	[[nodiscard]] size_type count(const LookupKey& key) const {
		return contains(key) ? 1 : 0;
	}

	void swap(hash_set& other) noexcept(noexcept(core_.swap(other.core_))) {
		core_.swap(other.core_);
	}

	iterator find(const Key& key) const { return core_.find(key); }

	template <typename LookupKey>
		requires detail::heterogeneous_hash_lookup<Hash, Eq, Key, LookupKey>
	iterator find(const LookupKey& key) const {
		return core_.find(key);
	}

	[[nodiscard]] bool contains(const Key& key) const {
		return core_.contains(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_hash_lookup<Hash, Eq, Key, LookupKey>
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
		requires detail::heterogeneous_hash_lookup<Hash, Eq, Key, LookupKey>
	bool erase(const LookupKey& key) {
		return core_.erase_key(key);
	}

	iterator erase(iterator position) {
		return iterator(core_.erase(position));
	}

	void clear() noexcept { core_.clear(); }
	void reserve(size_type element_count) { core_.reserve(element_count); }
	void rehash(size_type bucket_count) { core_.rehash(bucket_count); }
	iterator begin() noexcept { return core_.cbegin(); }
	iterator end() noexcept { return core_.cend(); }
	iterator begin() const noexcept { return core_.cbegin(); }
	iterator end() const noexcept { return core_.cend(); }

	const_iterator cbegin() const noexcept { return begin(); }
	const_iterator cend() const noexcept { return end(); }

 private:
	static std::pair<iterator, bool> convert_insert_result(
		std::pair<typename core_type::iterator, bool> result) {
		return {iterator(result.first), result.second};
	}

	core_type core_;
};

}  // 네임스페이스 nova::containers

namespace nova {

template <typename Key, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<Key>>
using hash_set = containers::hash_set<Key, Hash, Eq, Alloc>;

}  // 네임스페이스 nova

#endif  // NOVA_CONTAINERS_HASH_SET_HPP_
