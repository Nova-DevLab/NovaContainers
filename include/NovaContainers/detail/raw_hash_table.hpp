#ifndef NOVA_CONTAINERS_DETAIL_RAW_HASH_TABLE_HPP_
#define NOVA_CONTAINERS_DETAIL_RAW_HASH_TABLE_HPP_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/simd_util.hpp"

namespace nova::containers::detail {

template <typename StoredValue, typename Key, typename KeyOfValue,
		  typename Hash, typename Eq, typename Alloc>
	requires Hashable<Hash, Key> && KeyEquality<Key, Eq>
class RawHashTable {
 private:
	struct NoAllocateTag {};

	struct Slot {
		union {
			StoredValue value_;
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

 public:
	using value_type = StoredValue;
	using key_type = Key;
	using hasher = Hash;
	using key_equal = Eq;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using allocator_traits = std::allocator_traits<allocator_type>;

 private:
	using SlotAlloc =
		typename allocator_traits::template rebind_alloc<Slot>;
	using SlotTraits = std::allocator_traits<SlotAlloc>;
	static_assert(std::same_as<typename SlotTraits::pointer, Slot*>,
		"RawHashTable requires an allocator with raw Slot pointers");

 public:
	template <bool IsConst>
	class BasicIterator {
	 public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = RawHashTable::value_type;
		using difference_type = std::ptrdiff_t;
		using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;
		using reference =
			std::conditional_t<IsConst, const value_type&, value_type&>;

		BasicIterator() noexcept = default;
		BasicIterator(const BasicIterator&) noexcept = default;
		BasicIterator& operator=(const BasicIterator&) noexcept = default;

		template <bool OtherConst>
			 requires (IsConst && !OtherConst)
		BasicIterator(const BasicIterator<OtherConst>& other) noexcept
			: table_(other.table_), index_(other.index_) {}

		reference operator*() const noexcept {
			return table_->slots_[index_].value_;
		}

		pointer operator->() const noexcept {
			return &table_->slots_[index_].value_;
		}

		BasicIterator& operator++() noexcept {
			++index_;
			SkipEmpty();
			return *this;
		}

		BasicIterator operator++(int) noexcept {
			BasicIterator copy = *this;
			++(*this);
			return copy;
		}

		template <bool OtherConst>
		bool operator==(const BasicIterator<OtherConst>& other) const noexcept {
			return table_ == other.table_ && index_ == other.index_;
		}

		template <bool OtherConst>
		bool operator!=(const BasicIterator<OtherConst>& other) const noexcept {
			return !(*this == other);
		}

	 private:
		using TablePointer =
			std::conditional_t<IsConst, const RawHashTable*, RawHashTable*>;

		BasicIterator(TablePointer table, size_type index) noexcept
			: table_(table), index_(index) {
			SkipEmpty();
		}

		void SkipEmpty() noexcept {
			if (table_ == nullptr || table_->ctrl_ == nullptr) {
				return;
			}
			while (index_ < table_->capacity_ && !table_->IsOccupied(index_)) {
				++index_;
			}
		}

		TablePointer table_ = nullptr;
		size_type index_ = 0;
		template <bool>
		friend class BasicIterator;
		friend class RawHashTable;
	};

	using iterator = BasicIterator<false>;
	using const_iterator = BasicIterator<true>;

	RawHashTable() : RawHashTable(NoAllocateTag{}, Hash{}, Eq{}, Alloc{}) {}

	explicit RawHashTable(size_type initial_capacity, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: hash_func_(hash), eq_func_(equal), allocator_(allocator) {
		if (initial_capacity != 0) {
			RehashStorage(NormalizeCapacity(initial_capacity));
		}
	}

	~RawHashTable() {
		ClearAndDeallocate();
	}

	RawHashTable(const RawHashTable& other)
		: hash_func_(other.hash_func_),
		  eq_func_(other.eq_func_),
		  allocator_(allocator_traits::select_on_container_copy_construction(
			  other.allocator_)) {
		try {
			if (other.capacity_ != 0) {
				AllocateStorage(other.capacity_);
			}
			for (const_iterator position = other.Begin();
				 position != other.End(); ++position) {
				const value_type& value = *position;
				Insert(value);
			}
		} catch (...) {
			ClearAndDeallocate();
			throw;
		}
	}

	RawHashTable(RawHashTable&& other) noexcept(
		std::is_nothrow_move_constructible_v<Hash> &&
		std::is_nothrow_move_constructible_v<Eq> &&
		std::is_nothrow_move_constructible_v<Alloc>)
		: hash_func_(std::move(other.hash_func_)),
		  eq_func_(std::move(other.eq_func_)),
		  allocator_(std::move(other.allocator_)) {
		StealStorage(other);
	}

	RawHashTable& operator=(const RawHashTable& other) {
		static_assert(std::is_nothrow_swappable_v<Hash>,
			"RawHashTable copy assignment requires a nothrow-swappable Hash");
		static_assert(std::is_nothrow_swappable_v<Eq>,
			"RawHashTable copy assignment requires a nothrow-swappable Eq");
		if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
			static_assert(std::is_nothrow_swappable_v<Alloc>,
				"RawHashTable copy assignment requires a nothrow-swappable Alloc");
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

		RawHashTable replacement(NoAllocateTag{}, other.hash_func_, other.eq_func_,
			replacement_allocator);
		if (other.capacity_ != 0) {
			replacement.AllocateStorage(other.capacity_);
		}
		for (const_iterator position = other.Begin();
			 position != other.End(); ++position) {
			const value_type& value = *position;
			replacement.Insert(value);
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

	RawHashTable& operator=(RawHashTable&& other) noexcept(
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
				"RawHashTable move assignment requires a nothrow-swappable Hash");
			static_assert(std::is_nothrow_swappable_v<Eq>,
				"RawHashTable move assignment requires a nothrow-swappable Eq");
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
			RawHashTable replacement(NoAllocateTag{}, other.hash_func_,
				other.eq_func_, allocator_);
			if (other.capacity_ != 0) {
				replacement.AllocateStorage(other.capacity_);
			}
			for (size_type index = 0; index < other.capacity_; ++index) {
				if (other.IsOccupied(index)) {
					replacement.Insert(
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

	void Swap(RawHashTable& other) noexcept(
		std::is_nothrow_swappable_v<Hash> &&
		std::is_nothrow_swappable_v<Eq> &&
		(allocator_traits::propagate_on_container_swap::value
			 ? std::is_nothrow_swappable_v<Alloc>
			 : allocator_traits::is_always_equal::value)) {
		static_assert(std::is_nothrow_swappable_v<Hash>,
			"RawHashTable::Swap requires a nothrow-swappable Hash");
		static_assert(std::is_nothrow_swappable_v<Eq>,
			"RawHashTable::Swap requires a nothrow-swappable Eq");
		if constexpr (allocator_traits::propagate_on_container_swap::value) {
			static_assert(std::is_nothrow_swappable_v<Alloc>,
				"RawHashTable::Swap requires a nothrow-swappable Alloc");
		}
		if constexpr (!allocator_traits::propagate_on_container_swap::value &&
			!allocator_traits::is_always_equal::value) {
			if (allocator_ != other.allocator_) {
				throw std::invalid_argument(
					"RawHashTable::Swap: incompatible allocators");
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

	template <typename LookupKey>
		requires Hashable<Hash, LookupKey> &&
			KeyEqualityWith<Eq, Key, LookupKey>
	iterator Find(const LookupKey& key) {
		const size_type index = FindIndex(key);
		return index == capacity_ ? End() : iterator(this, index);
	}

	template <typename LookupKey>
		requires Hashable<Hash, LookupKey> &&
			KeyEqualityWith<Eq, Key, LookupKey>
	const_iterator Find(const LookupKey& key) const {
		const size_type index = FindIndex(key);
		return index == capacity_ ? End() : const_iterator(this, index);
	}

	template <typename LookupKey>
		requires Hashable<Hash, LookupKey> &&
			KeyEqualityWith<Eq, Key, LookupKey>
	[[nodiscard]] bool Contains(const LookupKey& key) const {
		return FindIndex(key) != capacity_;
	}

	template <typename V>
	std::pair<iterator, bool> Insert(V&& value) {
		const Key& key = KeyFrom(value);
		return InsertWithKey(key, [&](value_type* destination) {
			std::construct_at(destination, std::forward<V>(value));
		});
	}

	template <typename LookupKey, typename Constructor>
		requires Hashable<Hash, LookupKey> &&
			KeyEqualityWith<Eq, Key, LookupKey> &&
			std::invocable<Constructor, value_type*>
	std::pair<iterator, bool> InsertWithKey(
		const LookupKey& key, Constructor&& constructor) {
		if (capacity_ == 0) {
			RehashStorage(kDefaultCapacity);
		}

		InsertLocation location = FindInsertLocation(key);
		if (location.found) {
			return {iterator(this, location.index), false};
		}
		if (growth_left_ == 0) {
			if (size_ < CapacityToGrowthLeft(capacity_)) {
				RehashStorage(capacity_);
			} else {
				RehashStorage(NextCapacity(capacity_));
			}
			location = FindInsertLocation(key);
			assert(!location.found);
		}

		const bool consumes_growth =
			ctrl_[location.index] == kEmptySlot;
		std::invoke(std::forward<Constructor>(constructor),
			&slots_[location.index].value_);
		if (consumes_growth) {
			--growth_left_;
		}
		SetControlByte(location.index, location.h2);
		++size_;
		return {iterator(this, location.index), true};
	}

	template <bool IsConst>
	iterator Erase(BasicIterator<IsConst> position) {
		assert(position.table_ == this && position.index_ < capacity_);
		const size_type index = position.index_;
		slots_[index].Destroy();
		SetControlByte(index, kDeletedSlot);
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

	iterator Begin() noexcept { return iterator(this, 0); }
	iterator End() noexcept { return iterator(this, capacity_); }
	const_iterator Begin() const noexcept { return const_iterator(this, 0); }
	const_iterator End() const noexcept {
		return const_iterator(this, capacity_);
	}
	const_iterator CBegin() const noexcept { return Begin(); }
	const_iterator CEnd() const noexcept { return End(); }

 private:
	RawHashTable(NoAllocateTag, const Hash& hash, const Eq& equal,
		const Alloc& allocator)
		: hash_func_(hash), eq_func_(equal), allocator_(allocator) {}

	static constexpr size_type kDefaultCapacity = Group::kWidth;
	static constexpr float kMaxLoadFactor = 0.875F;

	struct HashParts {
		size_type h1;
		int8_t h2;
	};

	struct InsertLocation {
		size_type index;
		int8_t h2;
		bool found;
	};

	static const Key& KeyFrom(const value_type& value) noexcept {
		return KeyOfValue{}(value);
	}

	static constexpr size_type CapacityToGrowthLeft(size_type capacity) noexcept {
		return capacity * 7 / 8;
	}

	static size_type NormalizeCapacity(size_type bucket_count) {
		if (bucket_count > std::numeric_limits<size_type>::max() / 2) {
			throw std::length_error("RawHashTable: capacity too large");
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
			throw std::length_error("RawHashTable: element count too large");
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
			throw std::length_error("RawHashTable: capacity too large");
		}
		return capacity == 0 ? kDefaultCapacity : capacity * 2;
	}

	static HashParts SplitHash(size_type hash_value) noexcept {
		const size_type h1 = hash_value;
		size_type folded_hash = hash_value ^ (hash_value >> 7);
		if constexpr (sizeof(size_type) > 4) {
			folded_hash ^= folded_hash >> 32;
		}
		return {h1, static_cast<int8_t>(folded_hash & 0x7F)};
	}

	[[nodiscard]] bool IsOccupied(size_type index) const noexcept {
		return ctrl_[index] != kEmptySlot && ctrl_[index] != kDeletedSlot;
	}

	void AllocateStorage(size_type new_capacity) {
		if (new_capacity >
			std::numeric_limits<size_type>::max() - Group::kWidth) {
			throw std::length_error("RawHashTable: capacity too large");
		}
		std::unique_ptr<int8_t[]> new_control =
			std::make_unique<int8_t[]>(new_capacity + Group::kWidth);
		std::fill_n(new_control.get(), new_capacity + Group::kWidth, kEmptySlot);

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

	void SwapStorage(RawHashTable& other) noexcept {
		using std::swap;
		swap(capacity_, other.capacity_);
		swap(size_, other.size_);
		swap(growth_left_, other.growth_left_);
		swap(ctrl_, other.ctrl_);
		swap(slots_, other.slots_);
	}

	void StealStorage(RawHashTable& other) noexcept {
		capacity_ = std::exchange(other.capacity_, 0);
		size_ = std::exchange(other.size_, 0);
		growth_left_ = std::exchange(other.growth_left_, 0);
		ctrl_ = std::exchange(other.ctrl_, nullptr);
		slots_ = std::exchange(other.slots_, nullptr);
	}

	template <typename LookupKey>
		requires Hashable<Hash, LookupKey> &&
			KeyEqualityWith<Eq, Key, LookupKey>
	[[nodiscard]] size_type FindIndex(const LookupKey& key) const {
		if (capacity_ == 0) {
			return capacity_;
		}
		const auto [h1, h2] = SplitHash(hash_func_(key));
		size_type sequence = h1 & (capacity_ - 1);
		while (true) {
			Group group(ctrl_ + sequence);
			BitMask matches = group.Match(h2);
			while (matches) {
				const size_type index =
					(sequence + matches.LowestSetBit()) & (capacity_ - 1);
				if (eq_func_(KeyFrom(slots_[index].value_), key)) [[likely]] {
					return index;
				}
				matches.ClearLowestSetBit();
			}
			if (group.MatchEmpty()) {
				return capacity_;
			}
			sequence = (sequence + Group::kWidth) & (capacity_ - 1);
		}
	}

	template <typename LookupKey>
		requires Hashable<Hash, LookupKey> &&
			KeyEqualityWith<Eq, Key, LookupKey>
	[[nodiscard]] InsertLocation FindInsertLocation(const LookupKey& key) const {
		const auto [h1, h2] = SplitHash(hash_func_(key));
		size_type sequence = h1 & (capacity_ - 1);
		size_type target_index = capacity_;
		bool found_target = false;
		while (true) {
			Group group(ctrl_ + sequence);
			BitMask matches = group.Match(h2);
			while (matches) {
				const size_type index =
					(sequence + matches.LowestSetBit()) & (capacity_ - 1);
				if (eq_func_(KeyFrom(slots_[index].value_), key)) [[likely]] {
					return {index, h2, true};
				}
				matches.ClearLowestSetBit();
			}

			if (!found_target) {
				BitMask available = group.MatchEmptyOrDeleted();
				if (available) {
					target_index =
						(sequence + available.LowestSetBit()) & (capacity_ - 1);
					found_target = true;
				}
			}
			if (group.MatchEmpty()) {
				assert(found_target && target_index < capacity_);
				return {target_index, h2, false};
			}
			sequence = (sequence + Group::kWidth) & (capacity_ - 1);
		}
	}

	void ResetControlBytes() noexcept {
		std::fill_n(ctrl_, capacity_ + Group::kWidth, kEmptySlot);
		SyncControlTail();
	}

	void SyncControlTail() noexcept {
		if (capacity_ != 0) {
			std::copy_n(ctrl_, Group::kWidth, ctrl_ + capacity_);
		}
	}

	void SetControlByte(size_type index, int8_t value) noexcept {
		ctrl_[index] = value;
		if (index < Group::kWidth) {
			ctrl_[capacity_ + index] = value;
		}
	}

	void RehashStorage(size_type new_capacity) {
		RawHashTable replacement(NoAllocateTag{}, hash_func_, eq_func_, allocator_);
		replacement.AllocateStorage(new_capacity);
		for (size_type index = 0; index < capacity_; ++index) {
			if (!IsOccupied(index)) {
				continue;
			}
			if constexpr (std::is_copy_constructible_v<value_type>) {
				replacement.Insert(slots_[index].value_);
			} else {
				replacement.Insert(std::move(slots_[index].value_));
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

}  // namespace nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_RAW_HASH_TABLE_HPP_
