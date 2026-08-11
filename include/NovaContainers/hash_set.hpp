#ifndef NOVA_CONTAINERS_HASH_SET_HPP_
#define NOVA_CONTAINERS_HASH_SET_HPP_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/simd_util.hpp"

namespace nova::containers {

template <typename Key, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<Key>>
	requires detail::Hashable<Hash, Key> && detail::KeyEquality<Key, Eq>
class HashSet {
 private:
	struct NoAllocateTag {};

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
	using allocator_traits = std::allocator_traits<allocator_type>;

 private:
	struct Slot {
		union {
			value_type value_;
		};

		Slot() noexcept {}
		~Slot() {}

		template <typename... Args>
		void Construct(Args&&... args) {
			std::construct_at(&value_, std::forward<Args>(args)...);
		}

		void Destroy() noexcept {
			std::destroy_at(&value_);
		}
	};

	using SlotAlloc =
		typename allocator_traits::template rebind_alloc<Slot>;
	using SlotTraits = std::allocator_traits<SlotAlloc>;
	static_assert(std::same_as<typename SlotTraits::pointer, Slot*>,
		"HashSet requires an allocator with raw Slot pointers");

 public:
	class ConstIterator {
	 public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = HashSet::value_type;
		using difference_type = std::ptrdiff_t;
		using pointer = const value_type*;
		using reference = const value_type&;

		ConstIterator() noexcept = default;

		reference operator*() const noexcept {
			return set_->slots_[index_].value_;
		}

		pointer operator->() const noexcept {
			return &set_->slots_[index_].value_;
		}

		ConstIterator& operator++() noexcept {
			++index_;
			SkipEmpty();
			return *this;
		}

		ConstIterator operator++(int) noexcept {
			ConstIterator copy = *this;
			++(*this);
			return copy;
		}

		bool operator==(const ConstIterator& other) const noexcept {
			return set_ == other.set_ && index_ == other.index_;
		}

		bool operator!=(const ConstIterator& other) const noexcept {
			return !(*this == other);
		}

	 private:
		ConstIterator(const HashSet* set, size_type index) noexcept
			: set_(set), index_(index) {
			SkipEmpty();
		}

		void SkipEmpty() noexcept {
			if (set_ == nullptr || set_->ctrl_ == nullptr) {
				return;
			}
			while (index_ < set_->capacity_ &&
				(set_->ctrl_[index_] == detail::kEmptySlot ||
				 set_->ctrl_[index_] == detail::kDeletedSlot)) {
				++index_;
			}
		}

		const HashSet* set_ = nullptr;
		size_type index_ = 0;
		friend class HashSet;
	};

	using iterator = ConstIterator;
	using const_iterator = ConstIterator;

	HashSet() : HashSet(NoAllocateTag{}, Hash{}, Eq{}, Alloc{}) {}

	explicit HashSet(size_type initial_capacity, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: hash_func_(hash), eq_func_(equal), allocator_(allocator) {
		if (initial_capacity != 0) {
			RehashStorage(NormalizeCapacity(initial_capacity));
		}
	}

	HashSet(std::initializer_list<value_type> init, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: HashSet(NoAllocateTag{}, hash, equal, allocator) {
		Reserve(init.size());
		for (const value_type& value : init) {
			Insert(value);
		}
	}

	~HashSet() {
		ClearAndDeallocate();
	}

	HashSet(const HashSet& other)
		: hash_func_(other.hash_func_),
		  eq_func_(other.eq_func_),
		  allocator_(allocator_traits::select_on_container_copy_construction(
			  other.allocator_)) {
		try {
			if (other.capacity_ != 0) {
				AllocateStorage(other.capacity_);
			}
			for (const value_type& value : other) {
				InsertInternal(value);
			}
		} catch (...) {
			ClearAndDeallocate();
			throw;
		}
	}

	HashSet(HashSet&& other) noexcept(
		std::is_nothrow_move_constructible_v<Hash> &&
		std::is_nothrow_move_constructible_v<Eq> &&
		std::is_nothrow_move_constructible_v<Alloc>)
		: hash_func_(std::move(other.hash_func_)),
		  eq_func_(std::move(other.eq_func_)),
		  allocator_(std::move(other.allocator_)) {
		StealStorage(other);
	}

	HashSet& operator=(const HashSet& other) {
		static_assert(std::is_nothrow_swappable_v<Hash>,
			"HashSet copy assignment requires a nothrow-swappable Hash");
		static_assert(std::is_nothrow_swappable_v<Eq>,
			"HashSet copy assignment requires a nothrow-swappable Eq");
		if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
			static_assert(std::is_nothrow_swappable_v<Alloc>,
				"HashSet copy assignment requires a nothrow-swappable Alloc");
		}

		if (this == &other) {
			return *this;
		}

		const Alloc& replacement_allocator = [&]() -> const Alloc& {
			if constexpr (
				allocator_traits::propagate_on_container_copy_assignment::value) {
				return other.allocator_;
			} else {
				return allocator_;
			}
		}();

		HashSet replacement(NoAllocateTag{}, other.hash_func_, other.eq_func_,
			replacement_allocator);
		if (other.capacity_ != 0) {
			replacement.AllocateStorage(other.capacity_);
		}
		for (const value_type& value : other) {
			replacement.InsertInternal(value);
		}

		using std::swap;
		swap(hash_func_, replacement.hash_func_);
		swap(eq_func_, replacement.eq_func_);
		if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
			swap(allocator_, replacement.allocator_);
		}
		SwapStorage(replacement);
		return *this;
	}

	HashSet& operator=(HashSet&& other) noexcept(
		std::is_nothrow_move_assignable_v<Hash> &&
		std::is_nothrow_move_assignable_v<Eq> &&
		(allocator_traits::propagate_on_container_move_assignment::value
			 ? std::is_nothrow_move_assignable_v<Alloc>
			 : allocator_traits::is_always_equal::value)) {
		if (this == &other) {
			return *this;
		}

		if constexpr (
			!allocator_traits::propagate_on_container_move_assignment::value &&
			!allocator_traits::is_always_equal::value) {
			static_assert(std::is_nothrow_swappable_v<Hash>,
				"HashSet move assignment requires a nothrow-swappable Hash");
			static_assert(std::is_nothrow_swappable_v<Eq>,
				"HashSet move assignment requires a nothrow-swappable Eq");
		}

		if constexpr (allocator_traits::propagate_on_container_move_assignment::value) {
			ClearAndDeallocate();
			hash_func_ = std::move(other.hash_func_);
			eq_func_ = std::move(other.eq_func_);
			allocator_ = std::move(other.allocator_);
			StealStorage(other);
		} else if constexpr (allocator_traits::is_always_equal::value) {
			ClearAndDeallocate();
			hash_func_ = std::move(other.hash_func_);
			eq_func_ = std::move(other.eq_func_);
			StealStorage(other);
		} else if (allocator_ == other.allocator_) {
			ClearAndDeallocate();
			hash_func_ = std::move(other.hash_func_);
			eq_func_ = std::move(other.eq_func_);
			StealStorage(other);
		} else {
			HashSet replacement(NoAllocateTag{}, other.hash_func_, other.eq_func_,
				allocator_);
			if (other.capacity_ != 0) {
				replacement.AllocateStorage(other.capacity_);
			}
			for (size_type index = 0; index < other.capacity_; ++index) {
				if (other.IsOccupied(index)) {
					replacement.InsertInternal(
						std::move(other.slots_[index].value_));
				}
			}

			using std::swap;
			swap(hash_func_, replacement.hash_func_);
			swap(eq_func_, replacement.eq_func_);
			SwapStorage(replacement);
			other.Clear();
		}
		return *this;
	}

	[[nodiscard]] size_type Size() const noexcept { return size_; }
	[[nodiscard]] size_type Capacity() const noexcept { return capacity_; }
	[[nodiscard]] bool Empty() const noexcept { return size_ == 0; }
	[[nodiscard]] float LoadFactor() const noexcept {
		return capacity_ == 0
			? 0.0F
			: static_cast<float>(size_) / static_cast<float>(capacity_);
	}
	[[nodiscard]] constexpr float MaxLoadFactor() const noexcept {
		return kMaxLoadFactor;
	}
	[[nodiscard]] allocator_type GetAllocator() const { return allocator_; }

	[[nodiscard]] size_type Count(const Key& key) const {
		return Contains(key) ? 1 : 0;
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] size_type Count(const LookupKey& key) const {
		return Contains(key) ? 1 : 0;
	}

	void Swap(HashSet& other) noexcept(
		std::is_nothrow_swappable_v<Hash> &&
		std::is_nothrow_swappable_v<Eq> &&
		(allocator_traits::propagate_on_container_swap::value
			 ? std::is_nothrow_swappable_v<Alloc>
			 : allocator_traits::is_always_equal::value)) {
		static_assert(std::is_nothrow_swappable_v<Hash>,
			"HashSet::Swap requires a nothrow-swappable Hash");
		static_assert(std::is_nothrow_swappable_v<Eq>,
			"HashSet::Swap requires a nothrow-swappable Eq");
		if constexpr (allocator_traits::propagate_on_container_swap::value) {
			static_assert(std::is_nothrow_swappable_v<Alloc>,
				"HashSet::Swap requires a nothrow-swappable Alloc");
		}
		if constexpr (!allocator_traits::propagate_on_container_swap::value &&
			!allocator_traits::is_always_equal::value) {
			if (allocator_ != other.allocator_) {
				throw std::invalid_argument(
					"nova::HashSet::Swap: incompatible allocators");
			}
		}

		using std::swap;
		swap(hash_func_, other.hash_func_);
		swap(eq_func_, other.eq_func_);
		if constexpr (allocator_traits::propagate_on_container_swap::value) {
			swap(allocator_, other.allocator_);
		}
		SwapStorage(other);
	}

	iterator Find(const Key& key) const {
		const size_type index = FindIndex(key);
		return index == capacity_ ? End() : iterator(this, index);
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	iterator Find(const LookupKey& key) const {
		const size_type index = FindIndex(key);
		return index == capacity_ ? End() : iterator(this, index);
	}

	[[nodiscard]] bool Contains(const Key& key) const {
		return FindIndex(key) != capacity_;
	}

	template <typename LookupKey>
		requires HeterogeneousLookup<LookupKey>
	[[nodiscard]] bool Contains(const LookupKey& key) const {
		return FindIndex(key) != capacity_;
	}

	template <typename... Args>
	std::pair<iterator, bool> Emplace(Args&&... args) {
		value_type value(std::forward<Args>(args)...);
		return InsertInternal(std::move(value));
	}

	std::pair<iterator, bool> Insert(const value_type& value) {
		return InsertInternal(value);
	}

	std::pair<iterator, bool> Insert(value_type&& value) {
		return InsertInternal(std::move(value));
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
		assert(position.set_ == this && position.index_ < capacity_);
		const size_type index = position.index_;
		slots_[index].Destroy();
		SetControlByte(index, detail::kDeletedSlot);
		--size_;
		return iterator(this, index + 1);
	}

	void Clear() noexcept {
		if (capacity_ == 0) {
			return;
		}
		for (size_type index = 0; index < capacity_; ++index) {
			if (IsOccupied(index)) {
				slots_[index].Destroy();
			}
		}
		ResetControlBytes();
		size_ = 0;
		growth_left_ = CapacityToGrowthLeft(capacity_);
	}

	void Reserve(size_type element_count) {
		const size_type required_capacity = CapacityForElementCount(element_count);
		if (required_capacity > capacity_) {
			RehashStorage(required_capacity);
		}
	}

	void Rehash(size_type bucket_count) {
		if (bucket_count == 0 && size_ == 0) {
			ClearAndDeallocate();
			return;
		}
		const size_type required_capacity = std::max(
			NormalizeCapacity(bucket_count), CapacityForElementCount(size_));
		if (required_capacity != capacity_ || TombstoneCount() != 0) {
			RehashStorage(required_capacity);
		}
	}

	iterator Begin() const noexcept { return iterator(this, 0); }
	iterator End() const noexcept { return iterator(this, capacity_); }

	[[nodiscard]] size_type size() const noexcept { return Size(); }
	[[nodiscard]] size_type capacity() const noexcept { return Capacity(); }
	[[nodiscard]] bool empty() const noexcept { return Empty(); }
	[[nodiscard]] float load_factor() const noexcept { return LoadFactor(); }
	[[nodiscard]] constexpr float max_load_factor() const noexcept {
		return MaxLoadFactor();
	}
	[[nodiscard]] allocator_type get_allocator() const { return GetAllocator(); }

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
	void rehash(size_type bucket_count) { Rehash(bucket_count); }

 private:
	template <typename LookupKey>
	static constexpr bool HeterogeneousLookup =
		!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
		detail::Transparent<Hash> && detail::Transparent<Eq> &&
		detail::Hashable<Hash, LookupKey> &&
		detail::KeyEqualityWith<Eq, Key, LookupKey>;

	HashSet(NoAllocateTag, const Hash& hash, const Eq& equal,
		const Alloc& allocator)
		: hash_func_(hash), eq_func_(equal), allocator_(allocator) {}

	static constexpr size_type kDefaultCapacity = detail::Group::kWidth;
	static constexpr float kMaxLoadFactor = 0.875F;

	static constexpr size_type CapacityToGrowthLeft(size_type capacity) noexcept {
		return capacity * 7 / 8;
	}

	static size_type NormalizeCapacity(size_type bucket_count) {
		if (bucket_count > std::numeric_limits<size_type>::max() / 2) {
			throw std::length_error("nova::HashSet: capacity too large");
		}
		size_type capacity = kDefaultCapacity;
		while (capacity < bucket_count) {
			capacity <<= 1;
		}
		return capacity;
	}

	static size_type CapacityForElementCount(size_type element_count) {
		if (element_count == 0) {
			return 0;
		}
		if (element_count > CapacityToGrowthLeft(
			std::numeric_limits<size_type>::max() / 2)) {
			throw std::length_error("nova::HashSet::Reserve: element count too large");
		}
		const size_type required_buckets =
			element_count + (element_count + 6) / 7;
		return NormalizeCapacity(required_buckets);
	}

	[[nodiscard]] size_type TombstoneCount() const noexcept {
		if (capacity_ == 0) {
			return 0;
		}
		return CapacityToGrowthLeft(capacity_) - growth_left_ - size_;
	}

	static size_type NextCapacity(size_type capacity) {
		if (capacity > std::numeric_limits<size_type>::max() / 2) {
			throw std::length_error("nova::HashSet: capacity too large");
		}
		return capacity == 0 ? kDefaultCapacity : capacity * 2;
	}

	struct HashParts {
		size_type h1;
		int8_t h2;
	};

	static HashParts SplitHash(size_type hash_value) noexcept {
		const size_type h1 = hash_value;
		size_type folded_hash = hash_value ^ (hash_value >> 7);
		if constexpr (sizeof(size_type) > 4) {
			folded_hash ^= folded_hash >> 32;
		}
		return {h1, static_cast<int8_t>(folded_hash & 0x7F)};
	}

	[[nodiscard]] bool IsOccupied(size_type index) const noexcept {
		return ctrl_[index] != detail::kEmptySlot &&
			ctrl_[index] != detail::kDeletedSlot;
	}

	void AllocateStorage(size_type new_capacity) {
		if (new_capacity >
			std::numeric_limits<size_type>::max() - detail::Group::kWidth) {
			throw std::length_error("nova::HashSet: capacity too large");
		}
		std::unique_ptr<int8_t[]> new_control =
			std::make_unique<int8_t[]>(new_capacity + detail::Group::kWidth);
		std::fill_n(new_control.get(), new_capacity + detail::Group::kWidth,
			detail::kEmptySlot);

		SlotAlloc slot_allocator(allocator_);
		Slot* new_slots = SlotTraits::allocate(slot_allocator, new_capacity);
		size_type constructed_count = 0;
		try {
			for (; constructed_count < new_capacity; ++constructed_count) {
				SlotTraits::construct(slot_allocator,
					&new_slots[constructed_count]);
			}
		} catch (...) {
			for (size_type index = 0; index < constructed_count; ++index) {
				SlotTraits::destroy(slot_allocator, &new_slots[index]);
			}
			SlotTraits::deallocate(slot_allocator, new_slots, new_capacity);
			throw;
		}

		capacity_ = new_capacity;
		size_ = 0;
		growth_left_ = CapacityToGrowthLeft(capacity_);
		ctrl_ = new_control.release();
		slots_ = new_slots;
		SyncControlTail();
	}

	void SwapStorage(HashSet& other) noexcept {
		using std::swap;
		swap(capacity_, other.capacity_);
		swap(size_, other.size_);
		swap(growth_left_, other.growth_left_);
		swap(ctrl_, other.ctrl_);
		swap(slots_, other.slots_);
	}

	void StealStorage(HashSet& other) noexcept {
		capacity_ = std::exchange(other.capacity_, 0);
		size_ = std::exchange(other.size_, 0);
		growth_left_ = std::exchange(other.growth_left_, 0);
		ctrl_ = std::exchange(other.ctrl_, nullptr);
		slots_ = std::exchange(other.slots_, nullptr);
	}

	template <typename LookupKey>
		requires detail::Hashable<Hash, LookupKey> &&
			detail::KeyEqualityWith<Eq, Key, LookupKey>
	[[nodiscard]] size_type FindIndex(const LookupKey& key) const {
		if (capacity_ == 0) {
			return capacity_;
		}

		const auto [h1, h2] = SplitHash(hash_func_(key));
		size_type sequence = h1 & (capacity_ - 1);
		while (true) {
			detail::Group group(ctrl_ + sequence);
			detail::BitMask matches = group.Match(h2);
			while (matches) {
				const size_type index =
					(sequence + matches.LowestSetBit()) & (capacity_ - 1);
				if (eq_func_(slots_[index].value_, key)) [[likely]] {
					return index;
				}
				matches.ClearLowestSetBit();
			}
			if (group.MatchEmpty()) {
				return capacity_;
			}
			sequence =
				(sequence + detail::Group::kWidth) & (capacity_ - 1);
		}
	}

	void ResetControlBytes() noexcept {
		std::fill_n(ctrl_, capacity_ + detail::Group::kWidth,
			detail::kEmptySlot);
		SyncControlTail();
	}

	void SyncControlTail() noexcept {
		if (capacity_ != 0) {
			std::copy_n(ctrl_, detail::Group::kWidth, ctrl_ + capacity_);
		}
	}

	void SetControlByte(size_type index, int8_t value) noexcept {
		ctrl_[index] = value;
		if (index < detail::Group::kWidth) {
			ctrl_[capacity_ + index] = value;
		}
	}

	template <typename V>
	std::pair<iterator, bool> InsertInternal(V&& value) {
		if (capacity_ == 0) {
			RehashStorage(kDefaultCapacity);
		} else if (growth_left_ == 0) {
			const size_type growth_limit = CapacityToGrowthLeft(capacity_);
			if (size_ < growth_limit) {
				RehashStorage(capacity_);
			} else {
				RehashStorage(NextCapacity(capacity_));
			}
		}

		const auto [h1, h2] = SplitHash(hash_func_(value));
		size_type sequence = h1 & (capacity_ - 1);
		size_type target_index = capacity_;
		bool found_target = false;
		while (true) {
			detail::Group group(ctrl_ + sequence);
			detail::BitMask matches = group.Match(h2);
			while (matches) {
				const size_type index =
					(sequence + matches.LowestSetBit()) & (capacity_ - 1);
				if (eq_func_(slots_[index].value_, value)) [[likely]] {
					return {iterator(this, index), false};
				}
				matches.ClearLowestSetBit();
			}

			if (!found_target) {
				detail::BitMask available = group.MatchEmptyOrDeleted();
				if (available) {
					target_index =
						(sequence + available.LowestSetBit()) & (capacity_ - 1);
					found_target = true;
				}
			}
			if (group.MatchEmpty()) {
				break;
			}
			sequence =
				(sequence + detail::Group::kWidth) & (capacity_ - 1);
		}

		assert(found_target && target_index < capacity_);
		const bool consumes_growth =
			ctrl_[target_index] == detail::kEmptySlot;
		slots_[target_index].Construct(std::forward<V>(value));
		if (consumes_growth) {
			--growth_left_;
		}
		SetControlByte(target_index, h2);
		++size_;
		return {iterator(this, target_index), true};
	}

	void RehashStorage(size_type new_capacity) {
		HashSet replacement(NoAllocateTag{}, hash_func_, eq_func_, allocator_);
		replacement.AllocateStorage(new_capacity);
		for (size_type index = 0; index < capacity_; ++index) {
			if (!IsOccupied(index)) {
				continue;
			}
			if constexpr (std::is_copy_constructible_v<value_type>) {
				replacement.InsertInternal(slots_[index].value_);
			} else {
				replacement.InsertInternal(std::move(slots_[index].value_));
			}
		}
		SwapStorage(replacement);
	}

	void ClearAndDeallocate() noexcept {
		if (capacity_ == 0) {
			return;
		}
		Clear();
		SlotAlloc slot_allocator(allocator_);
		for (size_type index = 0; index < capacity_; ++index) {
			SlotTraits::destroy(slot_allocator, &slots_[index]);
		}
		SlotTraits::deallocate(slot_allocator, slots_, capacity_);
		delete[] ctrl_;
		capacity_ = 0;
		size_ = 0;
		growth_left_ = 0;
		ctrl_ = nullptr;
		slots_ = nullptr;
	}

	size_type capacity_ = 0;
	size_type size_ = 0;
	size_type growth_left_ = 0;
	int8_t* ctrl_ = nullptr;
	Slot* slots_ = nullptr;
	[[no_unique_address]] Hash hash_func_{};
	[[no_unique_address]] Eq eq_func_{};
	[[no_unique_address]] Alloc allocator_{};
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
