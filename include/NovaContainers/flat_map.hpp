#ifndef NOVA_CONTAINERS_FLAT_MAP_HPP_
#define NOVA_CONTAINERS_FLAT_MAP_HPP_

#include <compare>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/flat_table.hpp"
#include "NovaContainers/detail/key_extractors.hpp"

namespace nova::containers {

template <typename Key, typename Value, typename Compare = std::less<>,
		  typename Alloc = std::allocator<std::pair<Key, Value>>>
	requires detail::KeyOrderedWith<Compare, Key, Key>
class FlatMap {
 public:
	using key_type = Key;
	using mapped_type = Value;
	using value_type = std::pair<Key, Value>;
	using key_compare = Compare;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

 private:
	using Core = detail::FlatTable<value_type, Key,
		detail::PairFirstKeyOfValue, Compare, Alloc>;

 public:
	template <bool IsConst>
	class BasicIterator {
	 private:
		using CoreIterator = std::conditional_t<IsConst,
			typename Core::const_iterator, typename Core::iterator>;

	 public:
		using iterator_category = std::random_access_iterator_tag;
		using iterator_concept = std::random_access_iterator_tag;
		using value_type = FlatMap::value_type;
		using difference_type = std::ptrdiff_t;
		using mapped_reference =
			std::conditional_t<IsConst, const Value&, Value&>;

		struct Reference {
			const Key& first;
			mapped_reference second;

			operator value_type() const
				requires std::copy_constructible<Key> &&
					std::copy_constructible<Value> {
				return value_type(first, second);
			}
		};

		class Pointer {
		 public:
			explicit Pointer(Reference reference) noexcept
				: reference_(reference) {}

			const Reference* operator->() const noexcept { return &reference_; }

		 private:
			Reference reference_;
		};

		using reference = Reference;
		using pointer = Pointer;

		BasicIterator() noexcept = default;
		BasicIterator(const BasicIterator&) noexcept = default;
		BasicIterator& operator=(const BasicIterator&) noexcept = default;

		template <bool OtherConst>
			requires (IsConst && !OtherConst)
		BasicIterator(const BasicIterator<OtherConst>& other) noexcept
			: iterator_(other.iterator_) {}

		reference operator*() const noexcept {
			return {iterator_->first, iterator_->second};
		}

		pointer operator->() const noexcept { return pointer(operator*()); }

		BasicIterator& operator++() noexcept {
			++iterator_;
			return *this;
		}

		BasicIterator operator++(int) noexcept {
			BasicIterator copy = *this;
			++(*this);
			return copy;
		}

		BasicIterator& operator--() noexcept {
			--iterator_;
			return *this;
		}

		BasicIterator operator--(int) noexcept {
			BasicIterator copy = *this;
			--(*this);
			return copy;
		}

		BasicIterator& operator+=(difference_type offset) noexcept {
			iterator_ += offset;
			return *this;
		}

		BasicIterator& operator-=(difference_type offset) noexcept {
			iterator_ -= offset;
			return *this;
		}

		BasicIterator operator+(difference_type offset) const noexcept {
			BasicIterator copy = *this;
			copy += offset;
			return copy;
		}

		BasicIterator operator-(difference_type offset) const noexcept {
			BasicIterator copy = *this;
			copy -= offset;
			return copy;
		}

		friend BasicIterator operator+(
			difference_type offset, BasicIterator iterator) noexcept {
			return iterator + offset;
		}

		reference operator[](difference_type offset) const noexcept {
			auto&& value = iterator_[offset];
			return {value.first, value.second};
		}

		template <bool OtherConst>
		difference_type operator-(
			const BasicIterator<OtherConst>& other) const noexcept {
			return iterator_ - other.iterator_;
		}

		template <bool OtherConst>
		bool operator==(const BasicIterator<OtherConst>& other) const noexcept {
			return iterator_ == other.iterator_;
		}

		template <bool OtherConst>
		auto operator<=>(const BasicIterator<OtherConst>& other) const noexcept {
			return iterator_ <=> other.iterator_;
		}

	 private:
		explicit BasicIterator(CoreIterator iterator) noexcept
			: iterator_(iterator) {}

		CoreIterator iterator_{};
		template <bool>
		friend class BasicIterator;
		friend class FlatMap;
	};

	using iterator = BasicIterator<false>;
	using const_iterator = BasicIterator<true>;
	using reference = typename iterator::reference;
	using const_reference = typename const_iterator::reference;
	using pointer = typename iterator::pointer;
	using const_pointer = typename const_iterator::pointer;
	using Iterator = iterator;
	using ConstIterator = const_iterator;

	FlatMap() = default;

	explicit FlatMap(const Compare& compare, const Alloc& allocator = Alloc{})
		: core_(compare, allocator) {}

	FlatMap(size_type initial_capacity, const Compare& compare = Compare{},
		const Alloc& allocator = Alloc{})
		: core_(initial_capacity, compare, allocator) {}

	FlatMap(std::initializer_list<value_type> init,
		const Compare& compare = Compare{}, const Alloc& allocator = Alloc{})
		: core_(init.size(), compare, allocator) {
		for (const value_type& value : init) {
			Insert(value);
		}
	}

	FlatMap(const FlatMap&) = default;
	FlatMap(FlatMap&&) = default;
	FlatMap& operator=(const FlatMap&) = default;
	FlatMap& operator=(FlatMap&&) = default;
	~FlatMap() = default;

	[[nodiscard]] size_type Size() const noexcept { return core_.Size(); }
	[[nodiscard]] size_type Capacity() const noexcept { return core_.Capacity(); }
	[[nodiscard]] bool Empty() const noexcept { return core_.Empty(); }
	[[nodiscard]] allocator_type GetAllocator() const {
		return core_.GetAllocator();
	}
	[[nodiscard]] key_compare KeyComp() const { return core_.KeyComp(); }

	[[nodiscard]] size_type Count(const Key& key) const {
		return Contains(key) ? 1 : 0;
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] size_type Count(const LookupKey& key) const {
		return Contains(key) ? 1 : 0;
	}

	void Swap(FlatMap& other) noexcept(noexcept(core_.Swap(other.core_))) {
		core_.Swap(other.core_);
	}

	iterator Find(const Key& key) { return iterator(core_.Find(key)); }
	const_iterator Find(const Key& key) const {
		return const_iterator(core_.Find(key));
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator Find(const LookupKey& key) {
		return iterator(core_.Find(key));
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	const_iterator Find(const LookupKey& key) const {
		return const_iterator(core_.Find(key));
	}

	[[nodiscard]] bool Contains(const Key& key) const {
		return core_.Contains(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] bool Contains(const LookupKey& key) const {
		return core_.Contains(key);
	}

	Value& At(const Key& key) {
		const iterator position = Find(key);
		if (position == End()) {
			throw std::out_of_range("nova::FlatMap::At: key not found");
		}
		return position->second;
	}

	const Value& At(const Key& key) const {
		const const_iterator position = Find(key);
		if (position == End()) {
			throw std::out_of_range("nova::FlatMap::At: key not found");
		}
		return position->second;
	}

	template <typename... Args>
	std::pair<iterator, bool> Emplace(Args&&... args) {
		value_type value(std::forward<Args>(args)...);
		return ConvertInsertResult(core_.Insert(std::move(value)));
	}

	template <typename... Args>
	std::pair<iterator, bool> TryEmplace(const Key& key, Args&&... args) {
		return ConvertInsertResult(core_.InsertWithKey(key, [&]() {
			return value_type(std::piecewise_construct,
				std::forward_as_tuple(key),
				std::forward_as_tuple(std::forward<Args>(args)...));
		}));
	}

	template <typename... Args>
	std::pair<iterator, bool> TryEmplace(Key&& key, Args&&... args) {
		return ConvertInsertResult(core_.InsertWithKey(key, [&]() {
			return value_type(std::piecewise_construct,
				std::forward_as_tuple(std::move(key)),
				std::forward_as_tuple(std::forward<Args>(args)...));
		}));
	}

	std::pair<iterator, bool> Insert(const value_type& value) {
		return ConvertInsertResult(core_.Insert(value));
	}

	std::pair<iterator, bool> Insert(value_type&& value) {
		return ConvertInsertResult(core_.Insert(std::move(value)));
	}

	template <typename M>
	std::pair<iterator, bool> InsertOrAssign(const Key& key, M&& value) {
		auto result = core_.InsertWithKey(key, [&]() {
			return value_type(key, std::forward<M>(value));
		});
		if (!result.second) {
			result.first->second = std::forward<M>(value);
		}
		return ConvertInsertResult(std::move(result));
	}

	template <typename M>
	std::pair<iterator, bool> InsertOrAssign(Key&& key, M&& value) {
		auto result = core_.InsertWithKey(key, [&]() {
			return value_type(std::move(key), std::forward<M>(value));
		});
		if (!result.second) {
			result.first->second = std::forward<M>(value);
		}
		return ConvertInsertResult(std::move(result));
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
		return iterator(core_.Erase(position.iterator_));
	}

	iterator Erase(const_iterator position) {
		return iterator(core_.Erase(position.iterator_));
	}

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

	iterator LowerBound(const Key& key) {
		return iterator(core_.LowerBound(key));
	}
	const_iterator LowerBound(const Key& key) const {
		return const_iterator(core_.LowerBound(key));
	}
	iterator UpperBound(const Key& key) {
		return iterator(core_.UpperBound(key));
	}
	const_iterator UpperBound(const Key& key) const {
		return const_iterator(core_.UpperBound(key));
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator LowerBound(const LookupKey& key) {
		return iterator(core_.LowerBound(key));
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	const_iterator LowerBound(const LookupKey& key) const {
		return const_iterator(core_.LowerBound(key));
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator UpperBound(const LookupKey& key) {
		return iterator(core_.UpperBound(key));
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	const_iterator UpperBound(const LookupKey& key) const {
		return const_iterator(core_.UpperBound(key));
	}

	std::pair<iterator, iterator> EqualRange(const Key& key) {
		const auto [first, last] = core_.EqualRange(key);
		return {iterator(first), iterator(last)};
	}

	std::pair<const_iterator, const_iterator> EqualRange(const Key& key) const {
		const auto [first, last] = core_.EqualRange(key);
		return {const_iterator(first), const_iterator(last)};
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	std::pair<iterator, iterator> EqualRange(const LookupKey& key) {
		const auto [first, last] = core_.EqualRange(key);
		return {iterator(first), iterator(last)};
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	std::pair<const_iterator, const_iterator> EqualRange(
		const LookupKey& key) const {
		const auto [first, last] = core_.EqualRange(key);
		return {const_iterator(first), const_iterator(last)};
	}

	iterator Begin() noexcept { return iterator(core_.Begin()); }
	iterator End() noexcept { return iterator(core_.End()); }
	const_iterator Begin() const noexcept {
		return const_iterator(core_.Begin());
	}
	const_iterator End() const noexcept { return const_iterator(core_.End()); }

	[[nodiscard]] size_type size() const noexcept { return Size(); }
	[[nodiscard]] size_type capacity() const noexcept { return Capacity(); }
	[[nodiscard]] bool empty() const noexcept { return Empty(); }
	[[nodiscard]] allocator_type get_allocator() const { return GetAllocator(); }
	[[nodiscard]] key_compare key_comp() const { return KeyComp(); }
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
	const_iterator cbegin() const noexcept {
		return const_iterator(core_.CBegin());
	}
	const_iterator cend() const noexcept {
		return const_iterator(core_.CEnd());
	}

	iterator find(const Key& key) { return Find(key); }
	const_iterator find(const Key& key) const { return Find(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator find(const LookupKey& key) {
		return Find(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	const_iterator find(const LookupKey& key) const {
		return Find(key);
	}

	[[nodiscard]] bool contains(const Key& key) const { return Contains(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] bool contains(const LookupKey& key) const {
		return Contains(key);
	}

	Value& at(const Key& key) { return At(key); }
	const Value& at(const Key& key) const { return At(key); }

	template <typename... Args>
	std::pair<iterator, bool> emplace(Args&&... args) {
		return Emplace(std::forward<Args>(args)...);
	}

	template <typename... Args>
	std::pair<iterator, bool> try_emplace(const Key& key, Args&&... args) {
		return TryEmplace(key, std::forward<Args>(args)...);
	}

	template <typename... Args>
	std::pair<iterator, bool> try_emplace(Key&& key, Args&&... args) {
		return TryEmplace(std::move(key), std::forward<Args>(args)...);
	}

	std::pair<iterator, bool> insert(const value_type& value) {
		return Insert(value);
	}
	std::pair<iterator, bool> insert(value_type&& value) {
		return Insert(std::move(value));
	}

	template <typename M>
	std::pair<iterator, bool> insert_or_assign(const Key& key, M&& value) {
		return InsertOrAssign(key, std::forward<M>(value));
	}

	template <typename M>
	std::pair<iterator, bool> insert_or_assign(Key&& key, M&& value) {
		return InsertOrAssign(std::move(key), std::forward<M>(value));
	}

	bool erase(const Key& key) { return Erase(key); }

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	bool erase(const LookupKey& key) {
		return Erase(key);
	}

	iterator erase(iterator position) { return Erase(position); }
	iterator erase(const_iterator position) { return Erase(position); }
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

	iterator lower_bound(const Key& key) { return LowerBound(key); }
	const_iterator lower_bound(const Key& key) const { return LowerBound(key); }
	iterator upper_bound(const Key& key) { return UpperBound(key); }
	const_iterator upper_bound(const Key& key) const { return UpperBound(key); }
	std::pair<iterator, iterator> equal_range(const Key& key) {
		return EqualRange(key);
	}
	std::pair<const_iterator, const_iterator> equal_range(const Key& key) const {
		return EqualRange(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator lower_bound(const LookupKey& key) {
		return LowerBound(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	const_iterator lower_bound(const LookupKey& key) const {
		return LowerBound(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator upper_bound(const LookupKey& key) {
		return UpperBound(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	const_iterator upper_bound(const LookupKey& key) const {
		return UpperBound(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	std::pair<iterator, iterator> equal_range(const LookupKey& key) {
		return EqualRange(key);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	std::pair<const_iterator, const_iterator> equal_range(
		const LookupKey& key) const {
		return EqualRange(key);
	}

	Value& operator[](const Key& key) {
		return TryEmplace(key).first->second;
	}
	Value& operator[](Key&& key) {
		return TryEmplace(std::move(key)).first->second;
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

template <typename Key, typename Value, typename Compare = std::less<>,
		  typename Alloc = std::allocator<std::pair<Key, Value>>>
using flat_map = containers::FlatMap<Key, Value, Compare, Alloc>;

template <typename Key, typename Value, typename Compare = std::less<>,
		  typename Alloc = std::allocator<std::pair<Key, Value>>>
using FlatMap = containers::FlatMap<Key, Value, Compare, Alloc>;

}  // namespace nova

#endif  // NOVA_CONTAINERS_FLAT_MAP_HPP_
