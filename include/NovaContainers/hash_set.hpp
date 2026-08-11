#ifndef NOVA_CONTAINERS_HASH_SET_HPP_
#define NOVA_CONTAINERS_HASH_SET_HPP_

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/raw_hash_table.hpp"

namespace nova::containers::detail {

template <typename Key>
struct IdentityKeyOfValue {
	const Key& operator()(const Key& value) const noexcept { return value; }
};

}  // namespace nova::containers::detail

namespace nova::containers {

template <typename Key, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<Key>>
	requires detail::Hashable<Hash, Key> && detail::KeyEquality<Key, Eq>
class HashSet {
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
	using Core = detail::RawHashTable<value_type, Key,
		detail::IdentityKeyOfValue<Key>, Hash, Eq, Alloc>;

 public:
	using iterator = typename Core::const_iterator;
	using const_iterator = typename Core::const_iterator;
	using Iterator = iterator;
	using ConstIterator = const_iterator;

	HashSet() = default;

	explicit HashSet(size_type initial_capacity, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: core_(initial_capacity, hash, equal, allocator) {}

	HashSet(std::initializer_list<value_type> init, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: core_(0, hash, equal, allocator) {
		Reserve(init.size());
		for (const value_type& value : init) {
			Insert(value);
		}
	}

	HashSet(const HashSet&) = default;
	HashSet(HashSet&&) = default;
	HashSet& operator=(const HashSet&) = default;
	HashSet& operator=(HashSet&&) = default;
	~HashSet() = default;

	[[nodiscard]] size_type Size() const noexcept { return core_.Size(); }
	[[nodiscard]] size_type Capacity() const noexcept { return core_.Capacity(); }
	[[nodiscard]] bool Empty() const noexcept { return core_.Empty(); }
	[[nodiscard]] float LoadFactor() const noexcept { return core_.LoadFactor(); }
	[[nodiscard]] constexpr float MaxLoadFactor() const noexcept {
		return core_.MaxLoadFactor();
	}
	[[nodiscard]] allocator_type GetAllocator() const {
		return core_.GetAllocator();
	}

	[[nodiscard]] size_type Count(const Key& key) const {
		return Contains(key) ? 1 : 0;
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] size_type Count(const LookupKey& key) const {
		return Contains(key) ? 1 : 0;
	}

	void Swap(HashSet& other) noexcept(noexcept(core_.Swap(other.core_))) {
		core_.Swap(other.core_);
	}

	iterator Find(const Key& key) const { return core_.Find(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator Find(const LookupKey& key) const {
		return core_.Find(key);
	}

	[[nodiscard]] bool Contains(const Key& key) const {
		return core_.Contains(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] bool Contains(const LookupKey& key) const {
		return core_.Contains(key);
	}

	template <typename... Args>
	std::pair<iterator, bool> Emplace(Args&&... args) {
		value_type value(std::forward<Args>(args)...);
		return ConvertInsertResult(core_.Insert(std::move(value)));
	}

	std::pair<iterator, bool> Insert(const value_type& value) {
		return ConvertInsertResult(core_.Insert(value));
	}

	std::pair<iterator, bool> Insert(value_type&& value) {
		return ConvertInsertResult(core_.Insert(std::move(value)));
	}

	bool Erase(const Key& key) {
		const iterator position = Find(key);
		if (position == End()) {
			return false;
		}
		Erase(position);
		return true;
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	bool Erase(const LookupKey& key) {
		const iterator position = Find(key);
		if (position == End()) {
			return false;
		}
		Erase(position);
		return true;
	}

	iterator Erase(iterator position) {
		return iterator(core_.Erase(position));
	}

	void Clear() noexcept { core_.Clear(); }
	void Reserve(size_type element_count) { core_.Reserve(element_count); }
	void Rehash(size_type bucket_count) { core_.Rehash(bucket_count); }
	iterator Begin() const noexcept { return core_.CBegin(); }
	iterator End() const noexcept { return core_.CEnd(); }

	[[nodiscard]] size_type size() const noexcept { return Size(); }
	[[nodiscard]] size_type capacity() const noexcept { return Capacity(); }
	[[nodiscard]] bool empty() const noexcept { return Empty(); }
	[[nodiscard]] size_type count(const Key& key) const { return Count(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] size_type count(const LookupKey& key) const {
		return Count(key);
	}

	[[nodiscard]] float load_factor() const noexcept { return LoadFactor(); }
	[[nodiscard]] constexpr float max_load_factor() const noexcept {
		return MaxLoadFactor();
	}
	[[nodiscard]] allocator_type get_allocator() const {
		return GetAllocator();
	}

	iterator begin() noexcept { return Begin(); }
	iterator end() noexcept { return End(); }
	const_iterator begin() const noexcept { return Begin(); }
	const_iterator end() const noexcept { return End(); }
	const_iterator cbegin() const noexcept { return Begin(); }
	const_iterator cend() const noexcept { return End(); }

	iterator find(const Key& key) const { return Find(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator find(const LookupKey& key) const {
		return Find(key);
	}

	[[nodiscard]] bool contains(const Key& key) const { return Contains(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] bool contains(const LookupKey& key) const {
		return Contains(key);
	}

	template <typename... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		return Emplace(std::forward<Args>(args)...);
	}

	std::pair<iterator, bool> insert(const value_type& value) {
		return Insert(value);
	}

	std::pair<iterator, bool> insert(value_type&& value) {
		return Insert(std::move(value));
	}

	bool erase(const Key& key) { return Erase(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	bool erase(const LookupKey& key) {
		return Erase(key);
	}

	iterator erase(iterator position) { return Erase(position); }
	void clear() noexcept { Clear(); }
	void reserve(size_type element_count) { Reserve(element_count); }
	void rehash(size_type bucket_count) { Rehash(bucket_count); }

 private:
	template <typename LookupKey>
	static constexpr bool HeterogeneousLookup =
		!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
		detail::Transparent<Hash> && detail::Transparent<Eq> &&
		detail::Hashable<Hash, LookupKey> &&
		detail::KeyEqualityWith<Eq, Key, LookupKey>;

	static std::pair<iterator, bool> ConvertInsertResult(
		std::pair<typename Core::iterator, bool> result) {
		return {iterator(result.first), result.second};
	}

	Core core_;
};

}  // namespace nova::containers

namespace nova {

template <typename Key, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<Key>>
using hash_set = containers::HashSet<Key, Hash, Eq, Alloc>;

template <typename Key, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<Key>>
using HashSet = containers::HashSet<Key, Hash, Eq, Alloc>;

}  // namespace nova

#endif  // NOVA_CONTAINERS_HASH_SET_HPP_
