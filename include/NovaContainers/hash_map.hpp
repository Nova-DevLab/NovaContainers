#ifndef NOVA_CONTAINERS_HASH_MAP_HPP_
#define NOVA_CONTAINERS_HASH_MAP_HPP_

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/key_extractors.hpp"
#include "NovaContainers/detail/hash/raw_hash_table.hpp"

namespace nova::containers {

template <typename Key, typename Value, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<std::pair<const Key, Value>>>
	requires detail::hashable<Hash, Key> && detail::key_equality<Key, Eq>
class hash_map {
 public:
	using key_type = Key;
	using mapped_type = Value;
	using value_type = std::pair<const Key, Value>;
	using hasher = Hash;
	using key_equal = Eq;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = value_type&;
	using const_reference = const value_type&;
	using pointer = typename std::allocator_traits<Alloc>::pointer;
	using const_pointer = typename std::allocator_traits<Alloc>::const_pointer;

 private:
	using core_type = detail::raw_hash_table<value_type, Key,
		detail::pair_first_key_of_value, Hash, Eq, Alloc>;

 public:
	using iterator = typename core_type::iterator;
	using const_iterator = typename core_type::const_iterator;

	hash_map() = default;

	explicit hash_map(size_type initial_capacity, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: core_(initial_capacity, hash, equal, allocator) {}

	hash_map(std::initializer_list<value_type> init, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: core_(0, hash, equal, allocator) {
		reserve(init.size());
		for (const value_type& value : init) {
			insert(value);
		}
	}

	hash_map(const hash_map&) = default;
	hash_map(hash_map&&) = default;
	hash_map& operator=(const hash_map&) = default;
	hash_map& operator=(hash_map&&) = default;
	~hash_map() = default;

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

	void swap(hash_map& other) noexcept(noexcept(core_.swap(other.core_))) {
		core_.swap(other.core_);
	}

	iterator find(const Key& key) { return core_.find(key); }
	const_iterator find(const Key& key) const { return core_.find(key); }

	template <typename LookupKey>
		requires detail::heterogeneous_hash_lookup<Hash, Eq, Key, LookupKey>
	iterator find(const LookupKey& key) {
		return core_.find(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_hash_lookup<Hash, Eq, Key, LookupKey>
	const_iterator find(const LookupKey& key) const {
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

	Value& at(const Key& key) {
		const iterator position = find(key);
		if (position == end()) {
			throw std::out_of_range("nova::hash_map::at: key not found");
		}
		return position->second;
	}

	const Value& at(const Key& key) const {
		const const_iterator position = find(key);
		if (position == end()) {
			throw std::out_of_range("nova::hash_map::at: key not found");
		}
		return position->second;
	}

	template <typename... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		value_type value(std::forward<Args>(args)...);
		return core_.insert(std::move(value));
	}

	template <typename... Args>
	std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
		return core_.insert_with_key(key, [&](value_type* destination) {
			std::construct_at(destination, std::piecewise_construct,
				std::forward_as_tuple(key),
				std::forward_as_tuple(std::forward<Args>(args)...));
		});
	}

	template <typename... Args>
	std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
		return core_.insert_with_key(key, [&](value_type* destination) {
			std::construct_at(destination, std::piecewise_construct,
				std::forward_as_tuple(std::move(key)),
				std::forward_as_tuple(std::forward<Args>(args)...));
		});
	}

	std::pair<iterator, bool> insert(const value_type& value) {
		return core_.insert(value);
	}

	std::pair<iterator, bool> insert(value_type&& value) {
		return core_.insert(std::move(value));
	}

	template <typename M>
	std::pair<iterator, bool> insert_or_assign(const Key& key, M&& value) {
		auto result = core_.insert_with_key(key, [&](value_type* destination) {
			std::construct_at(destination, std::piecewise_construct,
				std::forward_as_tuple(key),
				std::forward_as_tuple(std::forward<M>(value)));
		});
		if (!result.second) {
			result.first->second = std::forward<M>(value);
		}
		return result;
	}

	template <typename M>
	std::pair<iterator, bool> insert_or_assign(Key&& key, M&& value) {
		auto result = core_.insert_with_key(key, [&](value_type* destination) {
			std::construct_at(destination, std::piecewise_construct,
				std::forward_as_tuple(std::move(key)),
				std::forward_as_tuple(std::forward<M>(value)));
		});
		if (!result.second) {
			result.first->second = std::forward<M>(value);
		}
		return result;
	}

	bool erase(const Key& key) {
		return core_.erase_key(key);
	}

	template <typename LookupKey>
		requires detail::heterogeneous_hash_lookup<Hash, Eq, Key, LookupKey>
	bool erase(const LookupKey& key) {
		return core_.erase_key(key);
	}

	iterator erase(iterator position) { return core_.erase(position); }
	iterator erase(const_iterator position) { return core_.erase(position); }
	void clear() noexcept { core_.clear(); }
	void reserve(size_type element_count) { core_.reserve(element_count); }
	void rehash(size_type bucket_count) { core_.rehash(bucket_count); }

	iterator begin() noexcept { return core_.begin(); }
	iterator end() noexcept { return core_.end(); }
	const_iterator begin() const noexcept { return core_.begin(); }
	const_iterator end() const noexcept { return core_.end(); }

	const_iterator cbegin() const noexcept { return core_.cbegin(); }
	const_iterator cend() const noexcept { return core_.cend(); }

	Value& operator[](const Key& key) {
		return try_emplace(key).first->second;
	}

	Value& operator[](Key&& key) {
		return try_emplace(std::move(key)).first->second;
	}

 private:
	core_type core_;
};

}  // 네임스페이스 nova::containers

namespace nova {

template <typename Key, typename Value, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<std::pair<const Key, Value>>>
using hash_map = containers::hash_map<Key, Value, Hash, Eq, Alloc>;

}  // 네임스페이스 nova

#endif  // NOVA_CONTAINERS_HASH_MAP_HPP_
