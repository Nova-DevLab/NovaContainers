#ifndef NOVA_CONTAINERS_FLAT_SET_HPP_
#define NOVA_CONTAINERS_FLAT_SET_HPP_

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/flat_table.hpp"
#include "NovaContainers/detail/key_extractors.hpp"

namespace nova::containers {

template <typename Key, typename Compare = std::less<>,
		  typename Alloc = std::allocator<Key>>
	requires detail::KeyOrderedWith<Compare, Key, Key>
class FlatSet {
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
	using Core = detail::FlatTable<value_type, Key,
		detail::IdentityKeyOfValue, Compare, Alloc>;

 public:
	using iterator = typename Core::const_iterator;
	using const_iterator = typename Core::const_iterator;
	using Iterator = iterator;
	using ConstIterator = const_iterator;

	FlatSet() = default;

	explicit FlatSet(const Compare& compare, const Alloc& allocator = Alloc{})
		: core_(compare, allocator) {}

	FlatSet(size_type initial_capacity, const Compare& compare = Compare{},
		const Alloc& allocator = Alloc{})
		: core_(initial_capacity, compare, allocator) {}

	FlatSet(std::initializer_list<value_type> init,
		const Compare& compare = Compare{}, const Alloc& allocator = Alloc{})
		: core_(init.size(), compare, allocator) {
		for (const value_type& value : init) {
			Insert(value);
		}
	}

	FlatSet(const FlatSet&) = default;
	FlatSet(FlatSet&&) = default;
	FlatSet& operator=(const FlatSet&) = default;
	FlatSet& operator=(FlatSet&&) = default;
	~FlatSet() = default;

	[[nodiscard]] size_type Size() const noexcept { return core_.Size(); }
	[[nodiscard]] size_type Capacity() const noexcept { return core_.Capacity(); }
	[[nodiscard]] bool Empty() const noexcept { return core_.Empty(); }
	[[nodiscard]] allocator_type GetAllocator() const {
		return core_.GetAllocator();
	}
	[[nodiscard]] key_compare KeyComp() const { return core_.KeyComp(); }
	[[nodiscard]] value_compare ValueComp() const { return core_.KeyComp(); }

	[[nodiscard]] size_type Count(const Key& key) const {
		return Contains(key) ? 1 : 0;
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] size_type Count(const LookupKey& key) const {
		return Contains(key) ? 1 : 0;
	}

	void Swap(FlatSet& other) noexcept(noexcept(core_.Swap(other.core_))) {
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

	iterator Erase(iterator position) { return core_.Erase(position); }
	void Clear() noexcept { core_.Clear(); }
	void Reserve(size_type element_count) { core_.Reserve(element_count); }
	void ShrinkToFit() { core_.ShrinkToFit(); }

	template <typename InputIt>
	void AssignUnsorted(InputIt first, InputIt last) {
		core_.AssignUnsorted(first, last);
	}

	template <typename InputIt>
	void AssignSortedUnique(InputIt first, InputIt last) {
		core_.AssignSortedUnique(first, last);
	}

	iterator LowerBound(const Key& key) const { return core_.LowerBound(key); }
	iterator UpperBound(const Key& key) const { return core_.UpperBound(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator LowerBound(const LookupKey& key) const {
		return core_.LowerBound(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator UpperBound(const LookupKey& key) const {
		return core_.UpperBound(key);
	}

	std::pair<iterator, iterator> EqualRange(const Key& key) const {
		return core_.EqualRange(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	std::pair<iterator, iterator> EqualRange(const LookupKey& key) const {
		return core_.EqualRange(key);
	}

	iterator Begin() const noexcept { return core_.CBegin(); }
	iterator End() const noexcept { return core_.CEnd(); }

	[[nodiscard]] size_type size() const noexcept { return Size(); }
	[[nodiscard]] size_type capacity() const noexcept { return Capacity(); }
	[[nodiscard]] bool empty() const noexcept { return Empty(); }
	[[nodiscard]] allocator_type get_allocator() const { return GetAllocator(); }
	[[nodiscard]] key_compare key_comp() const { return KeyComp(); }
	[[nodiscard]] value_compare value_comp() const { return ValueComp(); }
	[[nodiscard]] size_type count(const Key& key) const { return Count(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] size_type count(const LookupKey& key) const {
		return Count(key);
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
	void shrink_to_fit() { ShrinkToFit(); }

	template <typename InputIt>
	void assign_unsorted(InputIt first, InputIt last) {
		AssignUnsorted(first, last);
	}

	template <typename InputIt>
	void assign_sorted_unique(InputIt first, InputIt last) {
		AssignSortedUnique(first, last);
	}

	iterator lower_bound(const Key& key) const { return LowerBound(key); }
	iterator upper_bound(const Key& key) const { return UpperBound(key); }
	std::pair<iterator, iterator> equal_range(const Key& key) const {
		return EqualRange(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator lower_bound(const LookupKey& key) const {
		return LowerBound(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator upper_bound(const LookupKey& key) const {
		return UpperBound(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	std::pair<iterator, iterator> equal_range(const LookupKey& key) const {
		return EqualRange(key);
	}

 private:
	template <typename LookupKey>
	static constexpr bool HeterogeneousLookup =
		!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
		detail::Transparent<Compare> &&
		detail::KeyOrderedWith<Compare, Key, LookupKey>;

	static std::pair<iterator, bool> ConvertInsertResult(
		std::pair<typename Core::iterator, bool> result) {
		return {iterator(result.first), result.second};
	}

	Core core_;
};

}  // namespace nova::containers

namespace nova {

template <typename Key, typename Compare = std::less<>,
		  typename Alloc = std::allocator<Key>>
using flat_set = containers::FlatSet<Key, Compare, Alloc>;

template <typename Key, typename Compare = std::less<>,
		  typename Alloc = std::allocator<Key>>
using FlatSet = containers::FlatSet<Key, Compare, Alloc>;

}  // namespace nova

#endif  // NOVA_CONTAINERS_FLAT_SET_HPP_
