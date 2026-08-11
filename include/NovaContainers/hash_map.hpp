#ifndef NOVA_CONTAINERS_HASH_MAP_HPP_
#define NOVA_CONTAINERS_HASH_MAP_HPP_

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
#include <tuple>
#include <utility>

#include "NovaContainers/detail/concepts.hpp"
#include "NovaContainers/detail/simd_util.hpp"

namespace nova::containers {

template <typename Key, typename Value, typename Hash = std::hash<Key>,
          typename Eq = std::equal_to<Key>,
          typename Alloc = std::allocator<std::pair<const Key, Value>>>
  requires detail::Hashable<Hash, Key> && detail::KeyEquality<Key, Eq>
class HashMap {
 private:
  struct NoAllocateTag {};

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
      typename std::allocator_traits<Alloc>::template rebind_alloc<Slot>;
  using SlotTraits = std::allocator_traits<SlotAlloc>;
	static_assert(std::same_as<typename SlotTraits::pointer, Slot*>,
			  "HashMap requires an allocator with raw Slot pointers");

 public:
  // Forward Iterators
  class ConstIterator;

  class Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = HashMap::value_type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;

    Iterator() noexcept : map_(nullptr), index_(0) {}
    Iterator(HashMap* map, size_type index) noexcept
        : map_(map), index_(index) {
      SkipEmpty();
    }

    reference operator*() const noexcept {
      return map_->slots_[index_].value_;
    }

    pointer operator->() const noexcept {
      return &map_->slots_[index_].value_;
    }

    Iterator& operator++() noexcept {
      ++index_;
      SkipEmpty();
      return *this;
    }

    Iterator operator++(int) noexcept {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    bool operator==(const Iterator& other) const noexcept {
      return map_ == other.map_ && index_ == other.index_;
    }

    bool operator!=(const Iterator& other) const noexcept {
      return !(*this == other);
    }

   private:
    void SkipEmpty() noexcept {
      if (!map_ || !map_->ctrl_) return;
      while (index_ < map_->capacity_ &&
             (map_->ctrl_[index_] == detail::kEmptySlot ||
              map_->ctrl_[index_] == detail::kDeletedSlot)) {
        ++index_;
      }
    }

   HashMap* map_;
    size_type index_;
    friend class HashMap;
    friend class ConstIterator;
  };

  class ConstIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = HashMap::value_type;
    using difference_type = std::ptrdiff_t;
    using pointer = const value_type*;
    using reference = const value_type&;

    ConstIterator() noexcept : map_(nullptr), index_(0) {}
    ConstIterator(const HashMap* map, size_type index) noexcept
        : map_(map), index_(index) {
      SkipEmpty();
    }
    ConstIterator(const Iterator& other) noexcept
        : map_(other.map_), index_(other.index_) {}

    reference operator*() const noexcept {
      return map_->slots_[index_].value_;
    }

    pointer operator->() const noexcept {
      return &map_->slots_[index_].value_;
    }

    ConstIterator& operator++() noexcept {
      ++index_;
      SkipEmpty();
      return *this;
    }

    ConstIterator operator++(int) noexcept {
      ConstIterator tmp = *this;
      ++(*this);
      return tmp;
    }

    bool operator==(const ConstIterator& other) const noexcept {
      return map_ == other.map_ && index_ == other.index_;
    }

    bool operator!=(const ConstIterator& other) const noexcept {
      return !(*this == other);
    }

   private:
    void SkipEmpty() noexcept {
      if (!map_ || !map_->ctrl_) return;
      while (index_ < map_->capacity_ &&
             (map_->ctrl_[index_] == detail::kEmptySlot ||
              map_->ctrl_[index_] == detail::kDeletedSlot)) {
        ++index_;
      }
    }

    const HashMap* map_;
    size_type index_;
    friend class HashMap;
  };

  using iterator = Iterator;
  using const_iterator = ConstIterator;

  // Constructors & Destructor
  HashMap() : HashMap(NoAllocateTag{}, Hash{}, Eq{}, Alloc{}) {}

  explicit HashMap(size_type initial_capacity, const Hash& hash = Hash{},
				   const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
	  : capacity_(0),
		size_(0),
		growth_left_(0),
		ctrl_(nullptr),
		slots_(nullptr),
		hash_func_(hash),
		eq_func_(equal),
		allocator_(allocator) {
    if (initial_capacity != 0) {
	  RehashStorage(NormalizeCapacity(initial_capacity));
    }
  }

  HashMap(std::initializer_list<value_type> init, const Hash& hash = Hash{},
		  const Eq& equal = Eq{}, const Alloc& allocator = Alloc{})
	  : HashMap(NoAllocateTag{}, hash, equal, allocator) {
	Reserve(init.size());
    for (const auto& item : init) {
      Insert(item);
    }
  }

  ~HashMap() {
    ClearAndDeallocate();
  }

  HashMap(const HashMap& other)
	  : capacity_(0),
		size_(0),
		growth_left_(0),
		ctrl_(nullptr),
		slots_(nullptr),
		hash_func_(other.hash_func_),
		eq_func_(other.eq_func_),
		allocator_(allocator_traits::select_on_container_copy_construction(
			other.allocator_)) {
	if (other.capacity_ != 0) {
	  RehashStorage(other.capacity_);
	}
	try {
		for (const auto& item : other) {
			Insert(item);
		}
	} catch (...) {
		ClearAndDeallocate();
		throw;
	}
  }

  HashMap(HashMap&& other) noexcept(
	  std::is_nothrow_move_constructible_v<Hash> &&
	  std::is_nothrow_move_constructible_v<Eq> &&
	  std::is_nothrow_move_constructible_v<Alloc>)
      : capacity_(other.capacity_),
        size_(other.size_),
        growth_left_(other.growth_left_),
        ctrl_(other.ctrl_),
		slots_(other.slots_),
		hash_func_(std::move(other.hash_func_)),
		eq_func_(std::move(other.eq_func_)),
		allocator_(std::move(other.allocator_)) {
    other.capacity_ = 0;
    other.size_ = 0;
    other.growth_left_ = 0;
    other.ctrl_ = nullptr;
    other.slots_ = nullptr;
  }

  HashMap& operator=(const HashMap& other) {
	static_assert(std::is_nothrow_swappable_v<Hash>,
			  "HashMap copy assignment requires a nothrow-swappable Hash");
	static_assert(std::is_nothrow_swappable_v<Eq>,
			  "HashMap copy assignment requires a nothrow-swappable Eq");
	if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
	  static_assert(std::is_nothrow_swappable_v<Alloc>,
				"HashMap copy assignment requires a nothrow-swappable Alloc");
	}

	if (this == &other) {
	  return *this;
	}

	const Alloc& replacement_allocator = [&]() -> const Alloc& {
	  if constexpr (allocator_traits::propagate_on_container_copy_assignment::value) {
		return other.allocator_;
	  } else {
		return allocator_;
	  }
	}();

	HashMap replacement(NoAllocateTag{}, other.hash_func_, other.eq_func_,
						replacement_allocator);
	if (other.capacity_ != 0) {
	  replacement.RehashStorage(other.capacity_);
	}
	for (const auto& item : other) {
	  replacement.Insert(item);
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

  HashMap& operator=(HashMap&& other) noexcept(
	  std::is_nothrow_move_assignable_v<Hash> &&
	  std::is_nothrow_move_assignable_v<Eq> &&
	  (allocator_traits::propagate_on_container_move_assignment::value
		   ? std::is_nothrow_move_assignable_v<Alloc>
		   : allocator_traits::is_always_equal::value)) {
	if (this == &other) {
	  return *this;
	}

	if constexpr (!allocator_traits::propagate_on_container_move_assignment::value &&
			  !allocator_traits::is_always_equal::value) {
	  static_assert(std::is_nothrow_swappable_v<Hash>,
				"HashMap move assignment requires a nothrow-swappable Hash "
				"when allocators can differ");
	  static_assert(std::is_nothrow_swappable_v<Eq>,
				"HashMap move assignment requires a nothrow-swappable Eq "
				"when allocators can differ");
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
	  HashMap replacement(NoAllocateTag{}, other.hash_func_, other.eq_func_,
						  allocator_);
	  if (other.capacity_ != 0) {
		replacement.RehashStorage(other.capacity_);
	  }
	  for (size_type i = 0; i < other.capacity_; ++i) {
		if (other.ctrl_[i] != detail::kEmptySlot &&
			other.ctrl_[i] != detail::kDeletedSlot) {
		  replacement.InsertInternal(std::move(other.slots_[i].value_));
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

  // Google Style API
  [[nodiscard]] size_type Size() const noexcept { return size_; }
  [[nodiscard]] size_type Capacity() const noexcept { return capacity_; }
  [[nodiscard]] bool Empty() const noexcept { return size_ == 0; }
	[[nodiscard]] size_type Count(const Key& key) const {
	  return Contains(key) ? 1 : 0;
	}
	template <typename LookupKey>
	  requires (!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
				detail::Transparent<Hash> && detail::Transparent<Eq> &&
				detail::Hashable<Hash, LookupKey> &&
				detail::KeyEqualityWith<Eq, Key, LookupKey>)
	[[nodiscard]] size_type Count(const LookupKey& key) const {
	  return Contains(key) ? 1 : 0;
	}
	[[nodiscard]] float LoadFactor() const noexcept {
	  return capacity_ == 0
			 ? 0.0F
			 : static_cast<float>(size_) / static_cast<float>(capacity_);
	}
	[[nodiscard]] constexpr float MaxLoadFactor() const noexcept {
	  return kMaxLoadFactor;
	}
	[[nodiscard]] allocator_type GetAllocator() const { return allocator_; }

  void Swap(HashMap& other) noexcept(
	  std::is_nothrow_swappable_v<Hash> &&
	  std::is_nothrow_swappable_v<Eq> &&
	  (allocator_traits::propagate_on_container_swap::value
		   ? std::is_nothrow_swappable_v<Alloc>
		   : allocator_traits::is_always_equal::value)) {
	static_assert(std::is_nothrow_swappable_v<Hash>,
			  "HashMap::Swap requires a nothrow-swappable Hash");
	static_assert(std::is_nothrow_swappable_v<Eq>,
			  "HashMap::Swap requires a nothrow-swappable Eq");
	if constexpr (allocator_traits::propagate_on_container_swap::value) {
	  static_assert(std::is_nothrow_swappable_v<Alloc>,
				"HashMap::Swap requires a nothrow-swappable Alloc");
	}

	if constexpr (!allocator_traits::propagate_on_container_swap::value &&
				  !allocator_traits::is_always_equal::value) {
	  if (allocator_ != other.allocator_) {
		throw std::invalid_argument(
			"nova::HashMap::Swap: incompatible allocators");
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

  Iterator Find(const Key& key) {
	const size_type index = FindIndex(key);
	return index == capacity_ ? End() : Iterator(this, index);
  }

	template <typename LookupKey>
	  requires (!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
				detail::Transparent<Hash> && detail::Transparent<Eq> &&
				detail::Hashable<Hash, LookupKey> &&
				detail::KeyEqualityWith<Eq, Key, LookupKey>)
	Iterator Find(const LookupKey& key) {
	  const size_type index = FindIndex(key);
	  return index == capacity_ ? End() : Iterator(this, index);
	}

  ConstIterator Find(const Key& key) const {
	const size_type index = FindIndex(key);
	return index == capacity_ ? End() : ConstIterator(this, index);
  }

	template <typename LookupKey>
	  requires (!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
				detail::Transparent<Hash> && detail::Transparent<Eq> &&
				detail::Hashable<Hash, LookupKey> &&
				detail::KeyEqualityWith<Eq, Key, LookupKey>)
	ConstIterator Find(const LookupKey& key) const {
	  const size_type index = FindIndex(key);
	  return index == capacity_ ? End() : ConstIterator(this, index);
	}

  [[nodiscard]] bool Contains(const Key& key) const {
	return FindIndex(key) != capacity_;
  }

	template <typename LookupKey>
	  requires (!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
				detail::Transparent<Hash> && detail::Transparent<Eq> &&
				detail::Hashable<Hash, LookupKey> &&
				detail::KeyEqualityWith<Eq, Key, LookupKey>)
	[[nodiscard]] bool Contains(const LookupKey& key) const {
	  return FindIndex(key) != capacity_;
	}

  Value& At(const Key& key) {
	const Iterator it = Find(key);
	if (it == End()) {
	  throw std::out_of_range("nova::HashMap::At: key not found");
	}
	return it->second;
  }

  const Value& At(const Key& key) const {
	const ConstIterator it = Find(key);
	if (it == End()) {
	  throw std::out_of_range("nova::HashMap::At: key not found");
	}
	return it->second;
  }

  template <typename... Args>
  std::pair<Iterator, bool> Emplace(Args&&... args) {
    value_type val(std::forward<Args>(args)...);
    return Insert(std::move(val));
  }

	template <typename... Args>
	std::pair<Iterator, bool> TryEmplace(const Key& key, Args&&... args) {
	  Iterator existing = Find(key);
	  if (existing != End()) {
		return {existing, false};
	  }
	  return Emplace(std::piecewise_construct, std::forward_as_tuple(key),
					 std::forward_as_tuple(std::forward<Args>(args)...));
	}

	template <typename... Args>
	std::pair<Iterator, bool> TryEmplace(Key&& key, Args&&... args) {
	  Iterator existing = Find(key);
	  if (existing != End()) {
		return {existing, false};
	  }
	  return Emplace(std::piecewise_construct,
					 std::forward_as_tuple(std::move(key)),
					 std::forward_as_tuple(std::forward<Args>(args)...));
	}

  std::pair<Iterator, bool> Insert(const value_type& value) {
    return InsertInternal(value);
  }

  std::pair<Iterator, bool> Insert(value_type&& value) {
    return InsertInternal(std::move(value));
  }

	template <typename M>
	std::pair<Iterator, bool> InsertOrAssign(const Key& key, M&& value) {
	  Iterator existing = Find(key);
	  if (existing != End()) {
		existing->second = std::forward<M>(value);
		return {existing, false};
	  }
	  return TryEmplace(key, std::forward<M>(value));
	}

	template <typename M>
	std::pair<Iterator, bool> InsertOrAssign(Key&& key, M&& value) {
	  Iterator existing = Find(key);
	  if (existing != End()) {
		existing->second = std::forward<M>(value);
		return {existing, false};
	  }
	  return TryEmplace(std::move(key), std::forward<M>(value));
	}

  bool Erase(const Key& key) {
    auto it = Find(key);
    if (it == End()) return false;

	Erase(it);
    return true;
  }

	template <typename LookupKey>
	  requires (!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
				detail::Transparent<Hash> && detail::Transparent<Eq> &&
				detail::Hashable<Hash, LookupKey> &&
				detail::KeyEqualityWith<Eq, Key, LookupKey>)
	bool Erase(const LookupKey& key) {
	  Iterator iterator = Find(key);
	  if (iterator == End()) {
		return false;
	  }
	  Erase(iterator);
	  return true;
	}

	Iterator Erase(Iterator pos) {
	  assert(pos.map_ == this && pos.index_ < capacity_);
	  const size_type index = pos.index_;
	  slots_[index].Destroy();
	  SetControlByte(index, detail::kDeletedSlot);
	  --size_;
	  return Iterator(this, index + 1);
	}

	Iterator Erase(ConstIterator pos) {
	  assert(pos.map_ == this && pos.index_ < capacity_);
	  return Erase(Iterator(this, pos.index_));
	}

  void Clear() noexcept {
    if (capacity_ == 0) return;
    for (size_type i = 0; i < capacity_; ++i) {
      if (ctrl_[i] != detail::kEmptySlot && ctrl_[i] != detail::kDeletedSlot) {
        slots_[i].Destroy();
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

  Iterator Begin() noexcept { return Iterator(this, 0); }
  Iterator End() noexcept { return Iterator(this, capacity_); }
  ConstIterator Begin() const noexcept { return ConstIterator(this, 0); }
  ConstIterator End() const noexcept { return ConstIterator(this, capacity_); }

  // STL Façade API
  [[nodiscard]] size_type size() const noexcept { return Size(); }
  [[nodiscard]] size_type capacity() const noexcept { return Capacity(); }
  [[nodiscard]] bool empty() const noexcept { return Empty(); }
	[[nodiscard]] size_type count(const Key& key) const { return Count(key); }
	template <typename LookupKey>
	[[nodiscard]] size_type count(const LookupKey& key) const
	  requires requires(const HashMap& map) { map.Count(key); }
	{
	  return Count(key);
	}
	[[nodiscard]] float load_factor() const noexcept { return LoadFactor(); }
	[[nodiscard]] constexpr float max_load_factor() const noexcept {
	  return MaxLoadFactor();
	}
	[[nodiscard]] allocator_type get_allocator() const { return GetAllocator(); }
  
  iterator begin() noexcept { return Begin(); }
  iterator end() noexcept { return End(); }
  const_iterator begin() const noexcept { return Begin(); }
  const_iterator end() const noexcept { return End(); }
  const_iterator cbegin() const noexcept { return Begin(); }
  const_iterator cend() const noexcept { return End(); }

  iterator find(const Key& key) { return Find(key); }
  const_iterator find(const Key& key) const { return Find(key); }
	template <typename LookupKey>
	iterator find(const LookupKey& key)
	  requires requires(HashMap& map) { map.Find(key); }
	{
	  return Find(key);
	}
	template <typename LookupKey>
	const_iterator find(const LookupKey& key) const
	  requires requires(const HashMap& map) { map.Find(key); }
	{
	  return Find(key);
	}

  [[nodiscard]] bool contains(const Key& key) const { return Contains(key); }
	template <typename LookupKey>
	[[nodiscard]] bool contains(const LookupKey& key) const
	  requires requires(const HashMap& map) { map.Contains(key); }
	{
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

  std::pair<iterator, bool> insert(const value_type& value) { return Insert(value); }
  std::pair<iterator, bool> insert(value_type&& value) { return Insert(std::move(value)); }

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
	bool erase(const LookupKey& key)
	  requires requires(HashMap& map) { map.Erase(key); }
	{
	  return Erase(key);
	}
	iterator erase(iterator pos) { return Erase(pos); }
	iterator erase(const_iterator pos) { return Erase(pos); }
  void clear() noexcept { Clear(); }
	void reserve(size_type element_count) { Reserve(element_count); }
	void rehash(size_type bucket_count) { Rehash(bucket_count); }

  Value& operator[](const Key& key) {
    auto [inserted_it, inserted] = TryEmplace(key);
    return inserted_it->second;
  }

  Value& operator[](Key&& key) {
    auto [inserted_it, inserted] = TryEmplace(std::move(key));
    return inserted_it->second;
  }

  HashMap(NoAllocateTag, const Hash& hash, const Eq& equal,
		  const Alloc& allocator)
	  : capacity_(0),
		size_(0),
		growth_left_(0),
		ctrl_(nullptr),
		slots_(nullptr),
		hash_func_(hash),
		eq_func_(equal),
		allocator_(allocator) {}

	static constexpr size_type kDefaultCapacity = detail::Group::kWidth;
	static constexpr float kMaxLoadFactor = 0.875F;

  static constexpr size_type CapacityToGrowthLeft(size_type cap) noexcept {
    return cap * 7 / 8;  // Load factor ~87.5%
  }

	static size_type NormalizeCapacity(size_type bucket_count) {
	  if (bucket_count > std::numeric_limits<size_type>::max() / 2) {
		throw std::length_error("nova::HashMap: capacity too large");
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
		throw std::length_error("nova::HashMap::Reserve: element count too large");
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
		throw std::length_error("nova::HashMap: capacity too large");
	  }
	  return capacity == 0 ? kDefaultCapacity : capacity * 2;
	}

  struct HashParts {
    size_type h1;
    int8_t h2;
  };

  static HashParts SplitHash(size_type hash_val) noexcept {
	// Keep identity hashes distributed across buckets and fold upper bits into
	// the fingerprint so keys that share a bucket do not share h2 as easily.
	const size_type h1 = hash_val;
	size_type folded_hash = hash_val ^ (hash_val >> 7);
	if constexpr (sizeof(size_type) > 4) {
	  folded_hash ^= folded_hash >> 32;
	}
	const int8_t h2 = static_cast<int8_t>(folded_hash & 0x7F);
	return {h1, h2};
  }

  void AllocateStorage(size_type new_capacity) {
	if (new_capacity >
		std::numeric_limits<size_type>::max() - detail::Group::kWidth) {
	  throw std::length_error("nova::HashMap: capacity too large");
	}
	std::unique_ptr<int8_t[]> new_ctrl =
		std::make_unique<int8_t[]>(new_capacity + detail::Group::kWidth);
	std::fill_n(new_ctrl.get(), new_capacity + detail::Group::kWidth,
				detail::kEmptySlot);

	SlotAlloc slot_alloc(allocator_);
	Slot* new_slots = SlotTraits::allocate(slot_alloc, new_capacity);
	size_type constructed_count = 0;
	try {
		for (; constructed_count < new_capacity; ++constructed_count) {
			SlotTraits::construct(slot_alloc,
							  &new_slots[constructed_count]);
		}
	} catch (...) {
		for (size_type i = 0; i < constructed_count; ++i) {
			SlotTraits::destroy(slot_alloc, &new_slots[i]);
		}
		SlotTraits::deallocate(slot_alloc, new_slots, new_capacity);
		throw;
	}

	capacity_ = new_capacity;
	size_ = 0;
	growth_left_ = CapacityToGrowthLeft(capacity_);
	ctrl_ = new_ctrl.release();
	slots_ = new_slots;
	SyncControlTail();
  }

  void SwapStorage(HashMap& other) noexcept {
	using std::swap;
	swap(capacity_, other.capacity_);
	swap(size_, other.size_);
	swap(growth_left_, other.growth_left_);
	swap(ctrl_, other.ctrl_);
	swap(slots_, other.slots_);
  }

  void StealStorage(HashMap& other) noexcept {
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
	  detail::BitMask mask = group.Match(h2);
	  while (mask) {
		const size_type slot_index =
			(sequence + mask.LowestSetBit()) & (capacity_ - 1);
		if (eq_func_(slots_[slot_index].value_.first, key)) [[likely]] {
		  return slot_index;
		}
		mask.ClearLowestSetBit();
	  }

	  if (group.MatchEmpty()) {
		return capacity_;
	  }
	  sequence = (sequence + detail::Group::kWidth) & (capacity_ - 1);
	}
  }

  void ResetControlBytes() noexcept {
    std::fill_n(ctrl_, capacity_ + detail::Group::kWidth, detail::kEmptySlot);
	SyncControlTail();
  }

	void SyncControlTail() noexcept {
	  if (capacity_ == 0) {
		return;
	  }
	  std::copy_n(ctrl_, detail::Group::kWidth, ctrl_ + capacity_);
	}

	void SetControlByte(size_type index, int8_t value) noexcept {
	  ctrl_[index] = value;
	  if (index < detail::Group::kWidth) {
		ctrl_[capacity_ + index] = value;
	  }
	}

  template <typename V>
  std::pair<Iterator, bool> InsertInternal(V&& value) {
	if (capacity_ == 0) {
	  RehashStorage(kDefaultCapacity);
	} else if (growth_left_ == 0) {
	  const size_type growth_limit = CapacityToGrowthLeft(capacity_);
	  if (capacity_ != 0 && size_ < growth_limit) {
		RehashStorage(capacity_);
	  } else {
		RehashStorage(NextCapacity(capacity_));
	  }
    }

    auto [h1, h2] = SplitHash(hash_func_(value.first));
    size_type seq = h1 & (capacity_ - 1);
    size_type target_idx = capacity_;
    bool found_target = false;

    while (true) {
      detail::Group group(ctrl_ + seq);
      auto mask = group.Match(h2);
      while (mask) {
        size_type slot_idx = (seq + mask.LowestSetBit()) & (capacity_ - 1);
        if (eq_func_(slots_[slot_idx].value_.first, value.first)) [[likely]] {
          return {Iterator(this, slot_idx), false};
        }
        mask.ClearLowestSetBit();
      }

      if (!found_target) {
        auto avail_mask = group.MatchEmptyOrDeleted();
        if (avail_mask) {
          target_idx = (seq + avail_mask.LowestSetBit()) & (capacity_ - 1);
          found_target = true;
        }
      }

      auto empty_mask = group.MatchEmpty();
      if (empty_mask) {
        break;
      }
      seq = (seq + detail::Group::kWidth) & (capacity_ - 1);
    }

    assert(found_target && target_idx < capacity_);

    const bool consumes_growth = ctrl_[target_idx] == detail::kEmptySlot;
    slots_[target_idx].Construct(std::forward<V>(value));
	if (consumes_growth) {
	  --growth_left_;
	}
	SetControlByte(target_idx, h2);

    ++size_;
    return {Iterator(this, target_idx), true};
  }

  void RehashStorage(size_type new_capacity) {
	HashMap replacement(NoAllocateTag{}, hash_func_, eq_func_, allocator_);
	replacement.AllocateStorage(new_capacity);

	for (size_type i = 0; i < capacity_; ++i) {
	  if (ctrl_[i] == detail::kEmptySlot ||
		  ctrl_[i] == detail::kDeletedSlot) {
		continue;
	  }

	  if constexpr (std::is_copy_constructible_v<value_type>) {
		replacement.InsertInternal(slots_[i].value_);
	  } else {
		replacement.InsertInternal(std::move(slots_[i].value_));
	  }
	}

	SwapStorage(replacement);
  }

  void ClearAndDeallocate() noexcept {
    if (capacity_ == 0) return;
    Clear();

    SlotAlloc slot_alloc(allocator_);
    for (size_type i = 0; i < capacity_; ++i) {
      SlotTraits::destroy(slot_alloc, &slots_[i]);
    }
    SlotTraits::deallocate(slot_alloc, slots_, capacity_);
    delete[] ctrl_;

    capacity_ = 0;
    size_ = 0;
    growth_left_ = 0;
    ctrl_ = nullptr;
    slots_ = nullptr;
  }

  size_type capacity_;
  size_type size_;
  size_type growth_left_;
  int8_t* ctrl_;
  Slot* slots_;

  [[no_unique_address]] Hash hash_func_{};
  [[no_unique_address]] Eq eq_func_{};
	[[no_unique_address]] Alloc allocator_{};
};

}  // namespace nova::containers

namespace nova {
template <typename Key, typename Value, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<std::pair<const Key, Value>>>
using hash_map = containers::HashMap<Key, Value, Hash, Eq, Alloc>;

template <typename Key, typename Value, typename Hash = std::hash<Key>,
		  typename Eq = std::equal_to<Key>,
		  typename Alloc = std::allocator<std::pair<const Key, Value>>>
using HashMap = containers::HashMap<Key, Value, Hash, Eq, Alloc>;
}  // namespace nova

#endif  // NOVA_CONTAINERS_HASH_MAP_HPP_
