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
#include "NovaContainers/detail/hash/simd_group.hpp"

namespace nova::containers::detail {

template <typename StoredValue, typename Key, typename KeyOfValue,
		  typename Hash, typename Eq, typename Alloc>
	requires hashable<Hash, Key> && key_equality<Key, Eq>
class raw_hash_table {
 private:
	struct no_allocate_tag {};

	struct slot {
		union {
			StoredValue value_;
		};

		slot() noexcept {}
		~slot() {}

		template <typename... Args>
		void construct(Args&&... args) {
			std::construct_at(&value_, std::forward<Args>(args)...);
		}

		void destroy() noexcept {
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
	using slot_allocator =
		typename allocator_traits::template rebind_alloc<slot>;
	using slot_traits = std::allocator_traits<slot_allocator>;
	static_assert(std::same_as<typename slot_traits::pointer, slot*>,
		"raw_hash_table requires an allocator with raw slot pointers");

 public:
	template <bool IsConst>
	class basic_iterator {
	 public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = raw_hash_table::value_type;
		using difference_type = std::ptrdiff_t;
		using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;
		using reference =
			std::conditional_t<IsConst, const value_type&, value_type&>;

		basic_iterator() noexcept = default;
		basic_iterator(const basic_iterator&) noexcept = default;
		basic_iterator& operator=(const basic_iterator&) noexcept = default;

		template <bool OtherConst>
			 requires (IsConst && !OtherConst)
		basic_iterator(const basic_iterator<OtherConst>& other) noexcept
			: table_(other.table_), index_(other.index_) {}

		reference operator*() const noexcept {
			return table_->slots_[index_].value_;
		}

		pointer operator->() const noexcept {
			return &table_->slots_[index_].value_;
		}

		basic_iterator& operator++() noexcept {
			++index_;
			skip_empty();
			return *this;
		}

		basic_iterator operator++(int) noexcept {
			basic_iterator copy = *this;
			++(*this);
			return copy;
		}

		template <bool OtherConst>
		bool operator==(const basic_iterator<OtherConst>& other) const noexcept {
			return table_ == other.table_ && index_ == other.index_;
		}

		template <bool OtherConst>
		bool operator!=(const basic_iterator<OtherConst>& other) const noexcept {
			return !(*this == other);
		}

	 private:
		using table_pointer =
			std::conditional_t<IsConst, const raw_hash_table*, raw_hash_table*>;

		basic_iterator(table_pointer table, size_type index) noexcept
			: table_(table), index_(index) {
			skip_empty();
		}

		void skip_empty() noexcept {
			if (table_ == nullptr || table_->ctrl_ == nullptr) {
				return;
			}
			while (index_ < table_->capacity_ && !table_->is_occupied(index_)) {
				++index_;
			}
		}

		table_pointer table_ = nullptr;
		size_type index_ = 0;
		template <bool>
		friend class basic_iterator;
		friend class raw_hash_table;
	};

	using iterator = basic_iterator<false>;
	using const_iterator = basic_iterator<true>;

	raw_hash_table() : raw_hash_table(no_allocate_tag{}, Hash{}, Eq{}, Alloc{}) {}

	explicit raw_hash_table(size_type initial_capacity, const Hash& hash = Hash{},
		const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
		: hash_func_(hash), eq_func_(equal), allocator_(allocator) {
		if (initial_capacity != 0) {
			rehash_storage(normalize_capacity(initial_capacity));
		}
	}

	~raw_hash_table() {
		clear_and_deallocate();
	}

	raw_hash_table(const raw_hash_table& other)
		: hash_func_(other.hash_func_),
		  eq_func_(other.eq_func_),
		  allocator_(allocator_traits::select_on_container_copy_construction(
			  other.allocator_)) {
		try {
			copy_storage_from(other);
		} catch (...) {
			clear_and_deallocate();
			throw;
		}
	}

	raw_hash_table(raw_hash_table&& other) noexcept(
		std::is_nothrow_move_constructible_v<Hash> &&
		std::is_nothrow_move_constructible_v<Eq> &&
		std::is_nothrow_move_constructible_v<Alloc>)
		: hash_func_(std::move(other.hash_func_)),
		  eq_func_(std::move(other.eq_func_)),
		  allocator_(std::move(other.allocator_)) {
		steal_storage(other);
	}

	raw_hash_table& operator=(const raw_hash_table& other) {
		static_assert(std::is_nothrow_swappable_v<Hash>,
			"raw_hash_table copy assignment requires a nothrow-swappable Hash");
		static_assert(std::is_nothrow_swappable_v<Eq>,
			"raw_hash_table copy assignment requires a nothrow-swappable Eq");
		if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
			static_assert(std::is_nothrow_swappable_v<Alloc>,
				"raw_hash_table copy assignment requires a nothrow-swappable Alloc");
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

		raw_hash_table replacement(no_allocate_tag{}, other.hash_func_, other.eq_func_,
			replacement_allocator);
		replacement.copy_storage_from(other);

		using std::swap;
		swap(hash_func_, replacement.hash_func_);
		swap(eq_func_, replacement.eq_func_);
		if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
			swap(allocator_, replacement.allocator_);
		}
		swap_storage(replacement);
		return *this;
	}

	raw_hash_table& operator=(raw_hash_table&& other) noexcept(
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
				"raw_hash_table move assignment requires a nothrow-swappable Hash");
			static_assert(std::is_nothrow_swappable_v<Eq>,
				"raw_hash_table move assignment requires a nothrow-swappable Eq");
		}

		if constexpr (allocator_traits::propagate_on_container_move_assignment::value) {
			clear_and_deallocate();
			hash_func_ = std::move(other.hash_func_);
			eq_func_ = std::move(other.eq_func_);
			allocator_ = std::move(other.allocator_);
			steal_storage(other);
		} else if constexpr (allocator_traits::is_always_equal::value) {
			clear_and_deallocate();
			hash_func_ = std::move(other.hash_func_);
			eq_func_ = std::move(other.eq_func_);
			steal_storage(other);
		} else if (allocator_ == other.allocator_) {
			clear_and_deallocate();
			hash_func_ = std::move(other.hash_func_);
			eq_func_ = std::move(other.eq_func_);
			steal_storage(other);
		} else {
			raw_hash_table replacement(no_allocate_tag{}, other.hash_func_,
				other.eq_func_, allocator_);
			if (other.capacity_ != 0) {
				replacement.allocate_storage(other.capacity_);
			}
			for (size_type index = 0; index < other.capacity_; ++index) {
				if (other.is_occupied(index)) {
					replacement.insert(
						std::move(other.slots_[index].value_));
				}
			}

			using std::swap;
			swap(hash_func_, replacement.hash_func_);
			swap(eq_func_, replacement.eq_func_);
			swap_storage(replacement);
			other.clear();
		}
		return *this;
	}

	[[nodiscard]] size_type size() const noexcept { return size_; }
	[[nodiscard]] size_type capacity() const noexcept { return capacity_; }
	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] float load_factor() const noexcept {
		return capacity_ == 0
			? 0.0F
			: static_cast<float>(size_) / static_cast<float>(capacity_);
	}
	[[nodiscard]] constexpr float max_load_factor() const noexcept {
		return k_max_load_factor;
	}
	[[nodiscard]] allocator_type get_allocator() const { return allocator_; }

	void swap(raw_hash_table& other) noexcept(
		std::is_nothrow_swappable_v<Hash> &&
		std::is_nothrow_swappable_v<Eq> &&
		(allocator_traits::propagate_on_container_swap::value
			 ? std::is_nothrow_swappable_v<Alloc>
			 : allocator_traits::is_always_equal::value)) {
		static_assert(std::is_nothrow_swappable_v<Hash>,
			"raw_hash_table::Swap requires a nothrow-swappable Hash");
		static_assert(std::is_nothrow_swappable_v<Eq>,
			"raw_hash_table::Swap requires a nothrow-swappable Eq");
		if constexpr (allocator_traits::propagate_on_container_swap::value) {
			static_assert(std::is_nothrow_swappable_v<Alloc>,
				"raw_hash_table::Swap requires a nothrow-swappable Alloc");
		}
		if constexpr (!allocator_traits::propagate_on_container_swap::value &&
			!allocator_traits::is_always_equal::value) {
			if (allocator_ != other.allocator_) {
				throw std::invalid_argument(
					"raw_hash_table::Swap: incompatible allocators");
			}
		}

		using std::swap;
		swap(hash_func_, other.hash_func_);
		swap(eq_func_, other.eq_func_);
		if constexpr (allocator_traits::propagate_on_container_swap::value) {
			swap(allocator_, other.allocator_);
		}
		swap_storage(other);
	}

	template <typename LookupKey>
		requires hashable<Hash, LookupKey> &&
			key_equality_with<Eq, Key, LookupKey>
	iterator find(const LookupKey& key) {
		const size_type index = find_index(key);
		return index == capacity_ ? end() : iterator(this, index);
	}

	template <typename LookupKey>
		requires hashable<Hash, LookupKey> &&
			key_equality_with<Eq, Key, LookupKey>
	const_iterator find(const LookupKey& key) const {
		const size_type index = find_index(key);
		return index == capacity_ ? end() : const_iterator(this, index);
	}

	template <typename LookupKey>
		requires hashable<Hash, LookupKey> &&
			key_equality_with<Eq, Key, LookupKey>
	[[nodiscard]] bool contains(const LookupKey& key) const {
		return find_index(key) != capacity_;
	}

	template <typename V>
	std::pair<iterator, bool> insert(V&& value) {
		const Key& key = key_from(value);
		return insert_with_key(key, [&](value_type* destination) {
			std::construct_at(destination, std::forward<V>(value));
		});
	}

	template <typename LookupKey, typename Constructor>
		requires hashable<Hash, LookupKey> &&
			key_equality_with<Eq, Key, LookupKey> &&
			std::invocable<Constructor, value_type*>
	std::pair<iterator, bool> insert_with_key(
		const LookupKey& key, Constructor&& constructor) {
		if (capacity_ == 0) {
			rehash_storage(k_default_capacity);
		}

		insert_location location = find_insert_location(key);
		if (location.found) {
			return {iterator(this, location.index), false};
		}
		if (growth_left_ == 0) {
			if (size_ < capacity_to_growth_left(capacity_)) {
				rehash_storage(capacity_);
			} else {
				rehash_storage(next_capacity(capacity_));
			}
			location = find_insert_location(key);
			assert(!location.found);
		}

		const bool consumes_growth =
			ctrl_[location.index] == k_empty_slot;
		std::invoke(std::forward<Constructor>(constructor),
			&slots_[location.index].value_);
		if (consumes_growth) {
			--growth_left_;
		}
		set_control_byte(location.index, location.h2);
		++size_;
		return {iterator(this, location.index), true};
	}

	template <bool IsConst>
	iterator erase(basic_iterator<IsConst> position) {
		assert(position.table_ == this && position.index_ < capacity_);
		const size_type index = position.index_;
		slots_[index].destroy();
		set_control_byte(index, k_deleted_slot);
		--size_;
		return iterator(this, index + 1);
	}

	template <typename LookupKey>
		requires hashable<Hash, LookupKey> && key_equality_with<Eq, Key, LookupKey>
	bool erase_key(const LookupKey& key) {
		const iterator position = find(key);
		if (position == end()) {
			return false;
		}
		erase(position);
		return true;
	}

	void clear() noexcept {
		if (capacity_ == 0) {
			return;
		}
		for (size_type index = 0; index < capacity_; ++index) {
			if (is_occupied(index)) {
				slots_[index].destroy();
			}
		}
		reset_control_bytes();
		size_ = 0;
		growth_left_ = capacity_to_growth_left(capacity_);
	}

	void reserve(size_type element_count) {
		const size_type required_capacity = capacity_for_element_count(element_count);
		if (required_capacity > capacity_) {
			rehash_storage(required_capacity);
		}
	}

	void rehash(size_type bucket_count) {
		if (bucket_count == 0 && size_ == 0) {
			clear_and_deallocate();
			return;
		}
		const size_type required_capacity = std::max(
			normalize_capacity(bucket_count), capacity_for_element_count(size_));
		if (required_capacity != capacity_ || tombstone_count() != 0) {
			rehash_storage(required_capacity);
		}
	}

	iterator begin() noexcept { return iterator(this, 0); }
	iterator end() noexcept { return iterator(this, capacity_); }
	const_iterator begin() const noexcept { return const_iterator(this, 0); }
	const_iterator end() const noexcept {
		return const_iterator(this, capacity_);
	}
	const_iterator cbegin() const noexcept { return begin(); }
	const_iterator cend() const noexcept { return end(); }

 private:
	raw_hash_table(no_allocate_tag, const Hash& hash, const Eq& equal,
		const Alloc& allocator)
		: hash_func_(hash), eq_func_(equal), allocator_(allocator) {}

	static constexpr size_type k_default_capacity = group_type::k_width;
	static constexpr float k_max_load_factor = 0.875F;

	struct hash_parts {
		size_type h1;
		int8_t h2;
	};

	struct insert_location {
		size_type index;
		int8_t h2;
		bool found;
	};

	static const Key& key_from(const value_type& value) noexcept {
		return KeyOfValue{}(value);
	}

	static constexpr size_type capacity_to_growth_left(size_type capacity) noexcept {
		return capacity * 7 / 8;
	}

	static size_type normalize_capacity(size_type bucket_count) {
		if (bucket_count > std::numeric_limits<size_type>::max() / 2) {
			throw std::length_error("raw_hash_table: capacity too large");
		}
		size_type capacity = k_default_capacity;
		while (capacity < bucket_count) {
			capacity <<= 1;
		}
		return capacity;
	}

	static size_type capacity_for_element_count(size_type element_count) {
		if (element_count == 0) {
			return 0;
		}
		if (element_count > capacity_to_growth_left(
			std::numeric_limits<size_type>::max() / 2)) {
			throw std::length_error("raw_hash_table: element count too large");
		}
		const size_type required_buckets =
			element_count + (element_count + 6) / 7;
		return normalize_capacity(required_buckets);
	}

	[[nodiscard]] size_type tombstone_count() const noexcept {
		if (capacity_ == 0) {
			return 0;
		}
		return capacity_to_growth_left(capacity_) - growth_left_ - size_;
	}

	static size_type next_capacity(size_type capacity) {
		if (capacity > std::numeric_limits<size_type>::max() / 2) {
			throw std::length_error("raw_hash_table: capacity too large");
		}
		return capacity == 0 ? k_default_capacity : capacity * 2;
	}

	static hash_parts split_hash(size_type hash_value) noexcept {
		const size_type h1 = hash_value;
		size_type folded_hash = hash_value ^ (hash_value >> 7);
		if constexpr (sizeof(size_type) > 4) {
			folded_hash ^= folded_hash >> 32;
		}
		return {h1, static_cast<int8_t>(folded_hash & 0x7F)};
	}

	[[nodiscard]] bool is_occupied(size_type index) const noexcept {
		return ctrl_[index] != k_empty_slot && ctrl_[index] != k_deleted_slot;
	}

	void allocate_storage(size_type new_capacity) {
		if (new_capacity >
			std::numeric_limits<size_type>::max() - group_type::k_width) {
			throw std::length_error("raw_hash_table: capacity too large");
		}
		std::unique_ptr<int8_t[]> new_control =
			std::make_unique<int8_t[]>(new_capacity + group_type::k_width);
		std::fill_n(new_control.get(), new_capacity + group_type::k_width, k_empty_slot);

		slot_allocator slot_allocator(allocator_);
		slot* new_slots = slot_traits::allocate(slot_allocator, new_capacity);
		size_type constructed_count = 0;
		try {
			for (; constructed_count < new_capacity; ++constructed_count) {
				slot_traits::construct(slot_allocator,
					&new_slots[constructed_count]);
			}
		} catch (...) {
			for (size_type index = 0; index < constructed_count; ++index) {
				slot_traits::destroy(slot_allocator, &new_slots[index]);
			}
			slot_traits::deallocate(slot_allocator, new_slots, new_capacity);
			throw;
		}

		capacity_ = new_capacity;
		size_ = 0;
		growth_left_ = capacity_to_growth_left(capacity_);
		ctrl_ = new_control.release();
		slots_ = new_slots;
		sync_control_tail();
	}

	void copy_storage_from(const raw_hash_table& other) {
		if (other.capacity_ != 0) {
			allocate_storage(other.capacity_);
		}
		for (const_iterator position = other.begin();
			 position != other.end(); ++position) {
			const value_type& value = *position;
			insert(value);
		}
	}

	void swap_storage(raw_hash_table& other) noexcept {
		using std::swap;
		swap(capacity_, other.capacity_);
		swap(size_, other.size_);
		swap(growth_left_, other.growth_left_);
		swap(ctrl_, other.ctrl_);
		swap(slots_, other.slots_);
	}

	void steal_storage(raw_hash_table& other) noexcept {
		capacity_ = std::exchange(other.capacity_, 0);
		size_ = std::exchange(other.size_, 0);
		growth_left_ = std::exchange(other.growth_left_, 0);
		ctrl_ = std::exchange(other.ctrl_, nullptr);
		slots_ = std::exchange(other.slots_, nullptr);
	}

	template <typename LookupKey>
	size_type find_matching_index(bit_mask matches, size_type sequence,
		const LookupKey& key) const {
		while (matches) {
			const size_type index =
				(sequence + matches.lowest_set_bit()) & (capacity_ - 1);
			if (eq_func_(key_from(slots_[index].value_), key)) [[likely]] {
				return index;
			}
			matches.clear_lowest_set_bit();
		}
		return capacity_;
	}

	template <typename LookupKey>
		requires hashable<Hash, LookupKey> &&
			key_equality_with<Eq, Key, LookupKey>
	[[nodiscard]] size_type find_index(const LookupKey& key) const {
		if (capacity_ == 0) {
			return capacity_;
		}
		const auto [h1, h2] = split_hash(hash_func_(key));
		size_type sequence = h1 & (capacity_ - 1);
		while (true) {
			group_type group(ctrl_ + sequence);
			const size_type index = find_matching_index(group.match(h2), sequence, key);
			if (index != capacity_) {
				return index;
			}
			if (group.match_empty()) {
				return capacity_;
			}
			sequence = (sequence + group_type::k_width) & (capacity_ - 1);
		}
	}

	template <typename LookupKey>
		requires hashable<Hash, LookupKey> &&
			key_equality_with<Eq, Key, LookupKey>
	[[nodiscard]] insert_location find_insert_location(const LookupKey& key) const {
		const auto [h1, h2] = split_hash(hash_func_(key));
		size_type sequence = h1 & (capacity_ - 1);
		size_type target_index = capacity_;
		bool found_target = false;
		while (true) {
			group_type group(ctrl_ + sequence);
			const size_type index = find_matching_index(group.match(h2), sequence, key);
			if (index != capacity_) {
				return {index, h2, true};
			}

			if (!found_target) {
				bit_mask available = group.match_empty_or_deleted();
				if (available) {
					target_index =
						(sequence + available.lowest_set_bit()) & (capacity_ - 1);
					found_target = true;
				}
			}
			if (group.match_empty()) {
				assert(found_target && target_index < capacity_);
				return {target_index, h2, false};
			}
			sequence = (sequence + group_type::k_width) & (capacity_ - 1);
		}
	}

	void reset_control_bytes() noexcept {
		std::fill_n(ctrl_, capacity_ + group_type::k_width, k_empty_slot);
		sync_control_tail();
	}

	void sync_control_tail() noexcept {
		if (capacity_ != 0) {
			std::copy_n(ctrl_, group_type::k_width, ctrl_ + capacity_);
		}
	}

	void set_control_byte(size_type index, int8_t value) noexcept {
		ctrl_[index] = value;
		if (index < group_type::k_width) {
			ctrl_[capacity_ + index] = value;
		}
	}

	void rehash_storage(size_type new_capacity) {
		raw_hash_table replacement(no_allocate_tag{}, hash_func_, eq_func_, allocator_);
		replacement.allocate_storage(new_capacity);
		for (size_type index = 0; index < capacity_; ++index) {
			if (!is_occupied(index)) {
				continue;
			}
			if constexpr (std::is_copy_constructible_v<value_type>) {
				replacement.insert(slots_[index].value_);
			} else {
				replacement.insert(std::move(slots_[index].value_));
			}
		}
		swap_storage(replacement);
	}

	void clear_and_deallocate() noexcept {
		if (capacity_ == 0) {
			return;
		}
		clear();
		slot_allocator slot_allocator(allocator_);
		for (size_type index = 0; index < capacity_; ++index) {
			slot_traits::destroy(slot_allocator, &slots_[index]);
		}
		slot_traits::deallocate(slot_allocator, slots_, capacity_);
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
	slot* slots_ = nullptr;
	[[no_unique_address]] Hash hash_func_{};
	[[no_unique_address]] Eq eq_func_{};
	[[no_unique_address]] Alloc allocator_{};
};

}  // 네임스페이스 nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_RAW_HASH_TABLE_HPP_
