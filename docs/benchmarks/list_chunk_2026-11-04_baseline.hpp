#ifndef NOVA_LIST_CHUNK_BASELINE_HPP_
#define NOVA_LIST_CHUNK_BASELINE_HPP_

#include <algorithm>
#include <bit>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace nova_baseline::containers {

// 블록 인덱스로 임의 접근하고, 원소를 이동하지 않고 양끝으로 확장한다.
template <typename T, typename Alloc = std::allocator<T>,
	std::size_t BlockBytes = 16384>
class list {
	using traits = std::allocator_traits<Alloc>;
	static_assert(std::is_same_v<typename traits::value_type, T>);
	static_assert(std::is_same_v<typename traits::pointer, T*>,
		"list requires an allocator with raw pointers");

 public:
	using value_type = T;
	using allocator_type = Alloc;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = T&;
	using const_reference = const T&;
	using pointer = T*;
	using const_pointer = const T*;
	static constexpr size_type k_block_capacity =
		std::bit_floor(std::max(size_type{1}, BlockBytes / sizeof(T)));

 private:
	// 메인 배열의 조각은 빌린 포인터이며 추가 블록만 개별적으로 반환한다.
	struct block_entry {
		T* data = nullptr;
		bool owned = false;
	};

	template <bool IsConst>
	class basic_iterator {
		friend class list;
		template <bool> friend class basic_iterator;
		using element_type = std::conditional_t<IsConst, const T, T>;
		const block_entry* node_ = nullptr;
		element_type* current_ = nullptr;
		// 순회 중 변하지 않는 모드로 연속 경로를 별도 루프로 분리할 수 있게 한다.
		bool linear_ = true;

		basic_iterator(const block_entry* node, element_type* current) noexcept
			: node_(node), current_(current), linear_(node == nullptr) {}
		std::ptrdiff_t iterator_offset() const noexcept {
			return current_ == nullptr ? 0 : current_ - node_->data;
		}

	 public:
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using reference = element_type&;
		using pointer = element_type*;
		using iterator_category = std::random_access_iterator_tag;
		using iterator_concept = std::random_access_iterator_tag;

		basic_iterator() noexcept = default;
		template <bool OtherConst>
			requires (IsConst && !OtherConst)
		basic_iterator(const basic_iterator<OtherConst>& other) noexcept
			: node_(other.node_), current_(other.current_), linear_(other.linear_) {}

		reference operator*() const noexcept { return *current_; }
		pointer operator->() const noexcept { return current_; }
		reference operator[](difference_type n) const noexcept { return *(*this + n); }
		basic_iterator& operator++() noexcept {
			++current_;
			if (!linear_ && current_ == node_->data + k_block_capacity) {
				++node_;
				current_ = node_->data;
			}
			return *this;
		}
		basic_iterator operator++(int) noexcept { auto old = *this; ++*this; return old; }
		basic_iterator& operator--() noexcept {
			if (!linear_ && current_ == node_->data) {
				--node_;
				current_ = node_->data + k_block_capacity;
			}
			--current_;
			return *this;
		}
		basic_iterator operator--(int) noexcept { auto old = *this; --*this; return old; }
		basic_iterator& operator+=(difference_type n) noexcept {
			if (n == 0) return *this;
			if (linear_) { current_ += n; return *this; }
			const difference_type offset = iterator_offset() + n;
			constexpr difference_type width = static_cast<difference_type>(k_block_capacity);
			const difference_type block = offset >= 0 ? offset / width : -1 - (-1 - offset) / width;
			node_ += block;
			const difference_type local = offset - block * width;
			current_ = local == 0 ? node_->data : node_->data + local;
			return *this;
		}
		basic_iterator& operator-=(difference_type n) noexcept { return *this += -n; }
		friend basic_iterator operator+(basic_iterator it, difference_type n) noexcept { return it += n; }
		friend basic_iterator operator+(difference_type n, basic_iterator it) noexcept { return it += n; }
		friend basic_iterator operator-(basic_iterator it, difference_type n) noexcept { return it -= n; }
		template <bool OtherConst>
		difference_type operator-(const basic_iterator<OtherConst>& other) const noexcept {
			if (node_ == nullptr && other.node_ == nullptr) {
				return current_ == other.current_ ? 0 : current_ - other.current_;
			}
			if (node_ == other.node_) return iterator_offset() - other.iterator_offset();
			return (node_ - other.node_) * static_cast<difference_type>(k_block_capacity)
				+ iterator_offset() - other.iterator_offset();
		}
		template <bool OtherConst>
		bool operator==(const basic_iterator<OtherConst>& other) const noexcept {
			return current_ == other.current_;
		}
		template <bool OtherConst>
		std::strong_ordering operator<=>(const basic_iterator<OtherConst>& other) const noexcept {
			if (node_ == other.node_) return current_ <=> other.current_;
			return node_ <=> other.node_;
		}
	};

 public:
	using iterator = basic_iterator<false>;
	using const_iterator = basic_iterator<true>;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

 private:
	// 블록 경계를 바깥 루프로 옮기고 안쪽 루프는 연속 영역으로 처리한다.
	template <bool IsConst>
	class block_range {
		using owner_type = std::conditional_t<IsConst, const list, list>;
		using element_type = std::conditional_t<IsConst, const T, T>;
		owner_type* owner_;
	 public:
		explicit block_range(owner_type* owner) noexcept : owner_(owner) {}
		class block_iterator {
			owner_type* owner_ = nullptr;
			size_type position_ = 0;
		 public:
			using value_type = std::span<element_type>;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::input_iterator_tag;
			using iterator_concept = std::input_iterator_tag;
			block_iterator() = default;
			block_iterator(owner_type* owner, size_type position) noexcept
				: owner_(owner), position_(position) {}
			value_type operator*() const noexcept {
				if (owner_->contiguous_data_ != nullptr) {
					return {owner_->contiguous_data_ + position_, owner_->size_ - position_};
				}
				const size_type slot = owner_->first_ + position_;
				const size_type offset = slot % k_block_capacity;
				return {owner_->directory_[slot / k_block_capacity].data + offset,
					std::min(k_block_capacity - offset, owner_->size_ - position_)};
			}
			block_iterator& operator++() noexcept { position_ += (**this).size(); return *this; }
			block_iterator operator++(int) noexcept { auto old = *this; ++*this; return old; }
			bool operator==(const block_iterator&) const = default;
		};
		block_iterator begin() const noexcept { return {owner_, 0}; }
		block_iterator end() const noexcept { return {owner_, owner_->size_}; }
	};

 public:
	list() noexcept(std::is_nothrow_default_constructible_v<Alloc>) = default;
	explicit list(const Alloc& allocator) noexcept(std::is_nothrow_copy_constructible_v<Alloc>)
		: allocator_(allocator) {}
	explicit list(size_type count, const Alloc& allocator = Alloc{}) : list(allocator) {
		resize(count);
	}
	list(size_type count, const T& value, const Alloc& allocator = Alloc{}) : list(allocator) {
		resize(count, value);
	}
	template <std::input_iterator InputIt>
	list(InputIt first, InputIt last, const Alloc& allocator = Alloc{}) : list(allocator) {
		if constexpr (std::forward_iterator<InputIt>) {
			reserve_back(static_cast<size_type>(std::distance(first, last)));
		}
		for (; first != last; ++first) emplace_back(*first);
	}
	list(std::initializer_list<T> values, const Alloc& allocator = Alloc{})
		: list(values.begin(), values.end(), allocator) {}
	list(const list& other)
		: list(other.begin(), other.end(), traits::select_on_container_copy_construction(other.allocator_)) {}
	list(const list& other, const Alloc& allocator)
		: list(other.begin(), other.end(), allocator) {}
	list(list&& other) noexcept(std::is_nothrow_move_constructible_v<Alloc>)
		: allocator_(std::move(other.allocator_)), directory_(std::move(other.directory_)),
		  first_(std::exchange(other.first_, 0)), size_(std::exchange(other.size_, 0)),
		  allocated_begin_(std::exchange(other.allocated_begin_, 0)),
		  allocated_end_(std::exchange(other.allocated_end_, 0)),
		  main_(std::exchange(other.main_, nullptr)),
		  main_capacity_(std::exchange(other.main_capacity_, 0)),
		  contiguous_data_(std::exchange(other.contiguous_data_, nullptr)) {
		other.directory_.clear();
	}
	list(list&& other, const Alloc& allocator) : list(allocator) {
		if (allocator_ == other.allocator_) take_storage(other);
		else {
			reserve_back(other.size());
			for (T& value : other) emplace_back(std::move(value));
			other.clear();
		}
	}
	~list() { release(); }
	list& operator=(const list& other) {
		if (this == &other) return *this;
		if constexpr (traits::propagate_on_container_copy_assignment::value) {
			list temporary(other, other.allocator_);
			release();
			allocator_ = other.allocator_;
			take_storage(temporary);
		} else {
			list temporary(other, allocator_);
			swap_storage(temporary);
		}
		return *this;
	}
	list& operator=(list&& other) noexcept(
		traits::propagate_on_container_move_assignment::value
			? std::is_nothrow_move_assignable_v<Alloc> : traits::is_always_equal::value) {
		if (this == &other) return *this;
		if constexpr (traits::propagate_on_container_move_assignment::value) {
			release();
			allocator_ = std::move(other.allocator_);
			take_storage(other);
		} else if constexpr (traits::is_always_equal::value) {
			release();
			take_storage(other);
		} else if (allocator_ == other.allocator_) {
			release();
			take_storage(other);
		} else {
			list temporary(std::move(other), allocator_);
			swap_storage(temporary);
		}
		return *this;
	}
	list& operator=(std::initializer_list<T> values) { assign(values); return *this; }

	[[nodiscard]] size_type size() const noexcept { return size_; }
	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] size_type capacity() const noexcept {
		return (allocated_end_ - allocated_begin_) * k_block_capacity;
	}
	[[nodiscard]] size_type front_capacity() const noexcept {
		return directory_.empty() ? 0 : first_ - allocated_begin_ * k_block_capacity;
	}
	[[nodiscard]] size_type back_capacity() const noexcept {
		return directory_.empty() ? 0 : allocated_end_ * k_block_capacity - first_ - size_;
	}
	[[nodiscard]] size_type max_size() const noexcept {
		return std::min(traits::max_size(allocator_),
			static_cast<size_type>(std::numeric_limits<difference_type>::max()) / 4);
	}
	[[nodiscard]] allocator_type get_allocator() const { return allocator_; }
	reference operator[](size_type index) noexcept {
		if (contiguous_data_ != nullptr) return contiguous_data_[index];
		const size_type slot = first_ + index;
		return directory_[slot / k_block_capacity].data[slot % k_block_capacity];
	}
	const_reference operator[](size_type index) const noexcept {
		if (contiguous_data_ != nullptr) return contiguous_data_[index];
		const size_type slot = first_ + index;
		return directory_[slot / k_block_capacity].data[slot % k_block_capacity];
	}
	reference at(size_type index) {
		if (index >= size_) throw std::out_of_range("nova::list::at");
		return (*this)[index];
	}
	const_reference at(size_type index) const {
		if (index >= size_) throw std::out_of_range("nova::list::at");
		return (*this)[index];
	}
	reference front() noexcept { return (*this)[0]; }
	const_reference front() const noexcept { return (*this)[0]; }
	reference back() noexcept { return (*this)[size_ - 1]; }
	const_reference back() const noexcept { return (*this)[size_ - 1]; }
	iterator begin() noexcept { return iterator_at<false>(0); }
	iterator end() noexcept { return iterator_at<false>(size_); }
	const_iterator begin() const noexcept { return iterator_at<true>(0); }
	const_iterator end() const noexcept { return iterator_at<true>(size_); }
	const_iterator cbegin() const noexcept { return begin(); }
	const_iterator cend() const noexcept { return end(); }
	reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
	reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
	const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
	const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
	const_reverse_iterator crbegin() const noexcept { return rbegin(); }
	const_reverse_iterator crend() const noexcept { return rend(); }
	auto blocks() & noexcept { return block_range<false>(this); }
	auto blocks() const & noexcept { return block_range<true>(this); }
	auto blocks() && = delete;
	auto blocks() const && = delete;


	// 블록 내부를 연속 순회해 원소마다 블록 경계를 검사하지 않는다.
	// 콜백에서는 원소 값을 수정할 수 있지만 컨테이너 구조를 변경하면 안 된다.
	template <typename Function>
		requires std::invocable<Function&, T&>
	Function for_each(Function function) & {
		for (auto block : blocks()) {
			for (T& value : block) std::invoke(function, value);
		}
		return function;
	}
	template <typename Function>
		requires std::invocable<Function&, const T&>
	Function for_each(Function function) const & {
		for (auto block : blocks()) {
			for (const T& value : block) std::invoke(function, value);
		}
		return function;
	}
	template <typename Function> Function for_each(Function) && = delete;
	template <typename Function> Function for_each(Function) const && = delete;
	// 재구성은 순서를 유지하며, 실제 재구성 시 기존 참조와 iterator를 무효화한다.
	void optimize() {
		if (empty()) { release(); return; }
		const size_type block_count = (size_ - 1) / k_block_capacity + 1;
		const size_type slots = block_count * k_block_capacity;
		// 연속 상태여도 삭제 후 큰 메인 배열이나 예약 블록이 남아 있을 수 있다.
		if (main_ != nullptr && contiguous_data_ == main_ && main_capacity_ == slots
			&& allocated_begin_ == 1 && allocated_end_ == block_count + 1
			&& directory_.size() == block_count + 2) return;
		if (slots > traits::max_size(allocator_))
			throw std::length_error("nova::list optimized capacity");
		list compact(allocator_);
		compact.directory_.resize(block_count + 2);
		compact.first_ = k_block_capacity;
		compact.allocated_begin_ = 1;
		compact.allocated_end_ = block_count + 1;
		compact.main_ = traits::allocate(compact.allocator_, slots);
		compact.main_capacity_ = slots;
		for (size_type n = 0; n < block_count; ++n) {
			compact.directory_[n + 1] = {compact.main_ + n * k_block_capacity, false};
		}
		for (auto block : blocks()) {
			for (T& value : block) {
				traits::construct(compact.allocator_, compact.main_ + compact.size_,
					std::move_if_noexcept(value));
				++compact.size_;
			}
		}
		compact.refresh_contiguous();
		swap_storage(compact);
	}
	[[nodiscard]] bool is_contiguous() const noexcept {
		return empty() || contiguous_data_ != nullptr;
	}
	std::span<T> contiguous_view() & {
		if (!is_contiguous()) throw std::logic_error("nova::list is not contiguous");
		return {contiguous_data_, size_};
	}
	std::span<const T> contiguous_view() const & {
		if (!is_contiguous()) throw std::logic_error("nova::list is not contiguous");
		return {contiguous_data_, size_};
	}
	auto contiguous_view() && = delete;
	auto contiguous_view() const && = delete;

	void reserve_front(size_type count) { reserve_slots(count, 0); }
	void reserve_back(size_type count) { reserve_slots(0, count); }
	template <typename... Args>
	reference emplace_back(Args&&... args) {
		check_growth(1);
		if (back_capacity() == 0) reserve_back(1);
		const size_type slot = first_ + size_;
		pointer place = directory_[slot / k_block_capacity].data + slot % k_block_capacity;
		traits::construct(allocator_, place, std::forward<Args>(args)...);
		++size_;
		refresh_contiguous();
		return *place;
	}
	template <typename... Args>
	reference emplace_front(Args&&... args) {
		check_growth(1);
		if (front_capacity() == 0) reserve_front(1);
		const size_type slot = first_ - 1;
		pointer place = directory_[slot / k_block_capacity].data + slot % k_block_capacity;
		traits::construct(allocator_, place, std::forward<Args>(args)...);
		first_ = slot;
		++size_;
		refresh_contiguous();
		return *place;
	}
	void push_back(const T& value) { emplace_back(value); }
	void push_back(T&& value) { emplace_back(std::move(value)); }
	void push_front(const T& value) { emplace_front(value); }
	void push_front(T&& value) { emplace_front(std::move(value)); }
	void pop_front() noexcept {
		assert(!empty());
		traits::destroy(allocator_, std::addressof(front()));
		++first_;
		--size_;
		if (size_ == 0) reset_empty_position();
		else if (first_ % k_block_capacity == 0) release_front_blocks();
		refresh_contiguous();
	}
	void pop_back() noexcept {
		assert(!empty());
		traits::destroy(allocator_, std::addressof(back()));
		--size_;
		if (size_ == 0) reset_empty_position();
		else if ((first_ + size_) % k_block_capacity == 0) release_back_blocks();
		refresh_contiguous();
	}
	template <typename... Args>
	iterator emplace(const_iterator position, Args&&... args) {
		const size_type index = iterator_position(position);
		if (index == size_) { emplace_back(std::forward<Args>(args)...); return end() - 1; }
		if (index == 0) { emplace_front(std::forward<Args>(args)...); return begin(); }
		T value(std::forward<Args>(args)...);
		const size_type old_size = size_;
		if (index < size_ - index) {
			emplace_front(std::move(front()));
			try {
				std::move(begin() + 2, begin() + static_cast<difference_type>(index + 1), begin() + 1);
				(*this)[index] = std::move(value);
			} catch (...) { pop_front(); throw; }
		} else {
			emplace_back(std::move(back()));
			try {
				std::move_backward(begin() + static_cast<difference_type>(index),
					begin() + static_cast<difference_type>(old_size - 1),
					begin() + static_cast<difference_type>(old_size));
				(*this)[index] = std::move(value);
			} catch (...) { pop_back(); throw; }
		}
		return begin() + static_cast<difference_type>(index);
	}
	iterator insert(const_iterator position, const T& value) { return emplace(position, value); }
	iterator insert(const_iterator position, T&& value) { return emplace(position, std::move(value)); }
	iterator insert(const_iterator position, size_type count, const T& value) {
		const size_type index = iterator_position(position);
		check_growth(count);
		if (count == 0) return begin() + static_cast<difference_type>(index);
		const size_type previous = size_;
		// 양끝 확장은 기존 원소 주소를 유지하므로 내부 value 참조도 그대로 쓸 수 있다.
		// 2026-11-04 record64 10만 개 뒤 삽입 최고 요청량: 12,844,198 -> 6,444,159B.
		// 측정 조건과 한계는 docs/list_memory_optimization.md에 기록한다.
		if (index < size_ - index) {
			reserve_front(count);
			try {
				for (size_type n = 0; n < count; ++n) emplace_front(value);
			} catch (...) { while (size_ > previous) pop_front(); throw; }
			std::rotate(begin(), begin() + static_cast<difference_type>(count),
				begin() + static_cast<difference_type>(count + index));
		} else {
			reserve_back(count);
			try {
				for (size_type n = 0; n < count; ++n) emplace_back(value);
			} catch (...) { while (size_ > previous) pop_back(); throw; }
			std::rotate(begin() + static_cast<difference_type>(index),
				begin() + static_cast<difference_type>(previous), end());
		}
		return begin() + static_cast<difference_type>(index);
	}
	template <std::input_iterator InputIt>
	iterator insert(const_iterator position, InputIt first, InputIt last) {
		const size_type index = iterator_position(position);
		std::vector<T, Alloc> values(allocator_);
		if constexpr (std::sized_sentinel_for<InputIt, InputIt>) {
			const size_type count = static_cast<size_type>(last - first);
			check_growth(count);
			values.reserve(count);
		}
		for (; first != last; ++first) {
			if (values.size() == max_size() - size_) throw std::length_error("nova::list capacity");
			values.emplace_back(*first);
		}
		return insert_buffered(index, values);
	}
	iterator insert(const_iterator position, std::initializer_list<T> values) {
		return insert(position, values.begin(), values.end());
	}
	iterator erase(const_iterator position) { return erase(position, position + 1); }
	iterator erase(const_iterator first, const_iterator last) {
		const size_type index = iterator_position(first);
		const size_type finish = iterator_position(last);
		assert(finish >= index);
		const size_type count = finish - index;
		if (count == 0) return begin() + static_cast<difference_type>(index);
		if (index < size_ - finish) {
			std::move_backward(begin(), begin() + static_cast<difference_type>(index),
				begin() + static_cast<difference_type>(finish));
			for (size_type n = 0; n < count; ++n) pop_front();
		} else {
			std::move(begin() + static_cast<difference_type>(finish), end(),
				begin() + static_cast<difference_type>(index));
			for (size_type n = 0; n < count; ++n) pop_back();
		}
		return begin() + static_cast<difference_type>(index);
	}
	void clear() noexcept {
		for (auto block : blocks()) {
			if constexpr (!std::is_trivially_destructible_v<T> ||
				!std::is_same_v<Alloc, std::allocator<T>>) {
				for (T& value : block) traits::destroy(allocator_, std::addressof(value));
			}
		}
		size_ = 0;
		reset_empty_position();
		refresh_contiguous();
	}
	void resize(size_type count) { resize_impl(count, [this] { emplace_back(); }); }
	void resize(size_type count, const T& value) {
		if (count <= size_) { while (size_ > count) pop_back(); return; }
		// 내부 원소를 인자로 받아도 확장 도중 참조가 유지된다.
		resize_impl(count, [this, &value] { emplace_back(value); });
	}
	template <std::input_iterator InputIt>
	void assign(InputIt first, InputIt last) {
		list temporary(first, last, allocator_);
		swap_storage(temporary);
	}
	void assign(size_type count, const T& value) {
		list temporary(count, value, allocator_);
		swap_storage(temporary);
	}
	void assign(std::initializer_list<T> values) { assign(values.begin(), values.end()); }
	void swap(list& other) noexcept(traits::propagate_on_container_swap::value
		? std::is_nothrow_swappable_v<Alloc> : traits::is_always_equal::value) {
		if constexpr (traits::propagate_on_container_swap::value) {
			using std::swap;
			swap(allocator_, other.allocator_);
		} else if constexpr (!traits::is_always_equal::value) {
			if (!(allocator_ == other.allocator_))
				throw std::invalid_argument("nova::list allocator mismatch in swap");
		}
		swap_storage(other);
	}
	friend void swap(list& left, list& right) noexcept(noexcept(left.swap(right))) { left.swap(right); }
	friend bool operator==(const list& left, const list& right) {
		return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
	}


 private:
	template <bool IsConst>
	basic_iterator<IsConst> iterator_at(size_type index) const noexcept {
		if (size_ == 0) return {};
		if (contiguous_data_ != nullptr) return {nullptr, contiguous_data_ + index};
		const size_type slot = first_ + index;
		const block_entry* node = directory_.data() + slot / k_block_capacity;
		const size_type offset = slot % k_block_capacity;
		return {node, offset == 0 ? node->data : node->data + offset};
	}
	size_type iterator_position(const_iterator position) const noexcept {
		const difference_type index = position - cbegin();
		assert(index >= 0 && static_cast<size_type>(index) <= size_);
		return static_cast<size_type>(index);
	}
	void check_growth(size_type count) const {
		if (count > max_size() - size_) throw std::length_error("nova::list capacity");
	}
	void rebuild_directory(size_type front, size_type back) {
		const size_type allocated = allocated_end_ - allocated_begin_;
		const size_type before = directory_.empty() ? 0 : first_ - allocated_begin_ * k_block_capacity;
		const size_type after = directory_.empty() ? 0 : allocated_end_ * k_block_capacity - first_ - size_;
		const size_type extra_front = front > before ? (front - before - 1) / k_block_capacity + 1 : 0;
		const size_type extra_back = back > after ? (back - after - 1) / k_block_capacity + 1 : 0;
		const size_type needed = allocated + extra_front + extra_back + 2;
		const size_type limit = std::min(directory_.max_size(),
			static_cast<size_type>(std::numeric_limits<difference_type>::max()) / k_block_capacity);
		if (needed > limit) throw std::length_error("nova::list directory capacity");
		size_type length = std::max(size_type{8}, needed);
		// 고정 크기 큐에서는 디렉터리를 계속 키우지 않고 같은 크기로 재배치한다.
		if (needed <= directory_.size() / 2) {
			length = directory_.size();
		} else if (directory_.size() <= limit / 2) {
			length = std::max(length, directory_.size() * 2);
		}
		length = std::min(length, limit);
		std::vector<block_entry> replacement(length);
		const size_type new_begin = extra_front + (length - needed) / 2;
		for (size_type n = 0; n < allocated; ++n)
			replacement[new_begin + n] = directory_[allocated_begin_ + n];
		first_ = new_begin * k_block_capacity + before;
		allocated_begin_ = new_begin;
		allocated_end_ = new_begin + allocated;
		directory_.swap(replacement);
	}
	void reserve_slots(size_type front, size_type back) {
		check_growth(front);
		check_growth(back);
		if (front == 0 && back == 0) return;
		if (directory_.empty() || front > first_ ||
			back > (directory_.size() - 1) * k_block_capacity - first_ - size_)
			rebuild_directory(front, back);
		const size_type required_begin = (first_ - front) / k_block_capacity;
		const size_type required_end = (first_ + size_ + back + k_block_capacity - 1) / k_block_capacity;
		while (allocated_begin_ > required_begin) {
			pointer block = traits::allocate(allocator_, k_block_capacity);
			directory_[--allocated_begin_] = {block, true};
		}
		while (allocated_end_ < required_end) {
			pointer block = traits::allocate(allocator_, k_block_capacity);
			directory_[allocated_end_++] = {block, true};
		}
	}
	void refresh_contiguous() noexcept {
		if (empty()) { contiguous_data_ = nullptr; return; }
		const size_type first_block = first_ / k_block_capacity;
		const size_type last_block = (first_ + size_ - 1) / k_block_capacity;
		if (first_block == last_block ||
			(!directory_[first_block].owned && !directory_[last_block].owned)) {
			contiguous_data_ = directory_[first_block].data + first_ % k_block_capacity;
		} else {
			contiguous_data_ = nullptr;
		}
	}
	void release_block(size_type index) noexcept {
		block_entry& block = directory_[index];
		if (block.owned) traits::deallocate(allocator_, block.data, k_block_capacity);
		block = {};
	}
	void release_front_blocks() noexcept {
		const size_type keep = first_ / k_block_capacity;
		while (allocated_begin_ < keep) {
			if (!directory_[allocated_begin_].owned && main_ != nullptr) {
				// 살아 있는 메인 배열의 앞 여유는 다음 삽입에서 재사용한다.
				if (!directory_[keep].owned) break;
				// 모든 원소가 외부 블록으로 넘어가면 메인 배열 전체를 반환한다.
				traits::deallocate(allocator_, main_, main_capacity_);
				main_ = nullptr;
				main_capacity_ = 0;
			}
			release_block(allocated_begin_++);
		}
	}
	void release_back_blocks() noexcept {
		const size_type keep = (first_ + size_ + k_block_capacity - 1) / k_block_capacity;
		while (allocated_end_ > keep) {
			if (!directory_[allocated_end_ - 1].owned && main_ != nullptr) {
				// 살아 있는 메인 배열의 뒤 여유도 용량에 포함해 유지한다.
				if (!directory_[keep - 1].owned) break;
				traits::deallocate(allocator_, main_, main_capacity_);
				main_ = nullptr;
				main_capacity_ = 0;
			}
			--allocated_end_;
			release_block(allocated_end_);
		}
	}
	void reset_empty_position() noexcept {
		if (!directory_.empty()) {
			const size_type block = std::min(first_ / k_block_capacity,
				allocated_end_ > allocated_begin_ ? allocated_end_ - 1 : allocated_begin_);
			first_ = block * k_block_capacity;
		}
	}
	void release() noexcept {
		clear();
		for (size_type n = allocated_begin_; n < allocated_end_; ++n) release_block(n);
		if (main_ != nullptr) traits::deallocate(allocator_, main_, main_capacity_);
		main_ = contiguous_data_ = nullptr;
		main_capacity_ = 0;
		std::vector<block_entry>{}.swap(directory_);
		first_ = size_ = allocated_begin_ = allocated_end_ = 0;
	}
	void take_storage(list& other) noexcept {
		directory_ = std::move(other.directory_);
		first_ = std::exchange(other.first_, 0);
		size_ = std::exchange(other.size_, 0);
		allocated_begin_ = std::exchange(other.allocated_begin_, 0);
		allocated_end_ = std::exchange(other.allocated_end_, 0);
		main_ = std::exchange(other.main_, nullptr);
		main_capacity_ = std::exchange(other.main_capacity_, 0);
		contiguous_data_ = std::exchange(other.contiguous_data_, nullptr);
		other.directory_.clear();
	}
	void swap_storage(list& other) noexcept {
		using std::swap;
		directory_.swap(other.directory_);
		swap(first_, other.first_);
		swap(size_, other.size_);
		swap(allocated_begin_, other.allocated_begin_);
		swap(allocated_end_, other.allocated_end_);
		swap(main_, other.main_);
		swap(main_capacity_, other.main_capacity_);
		swap(contiguous_data_, other.contiguous_data_);
	}
	template <typename Append>
	void resize_impl(size_type count, Append append) {
		if (count <= size_) { while (size_ > count) pop_back(); return; }
		const size_type previous = size_;
		reserve_back(count - size_);
		try { while (size_ < count) append(); }
		catch (...) { while (size_ > previous) pop_back(); throw; }
	}
	iterator insert_buffered(size_type index, std::vector<T, Alloc>& values) {
		const size_type count = values.size();
		check_growth(count);
		if (count == 0) return begin() + static_cast<difference_type>(index);
		const size_type previous = size_;
		if (index < size_ - index) {
			reserve_front(count);
			try {
				for (auto it = values.rbegin(); it != values.rend(); ++it) emplace_front(std::move(*it));
			} catch (...) { while (size_ > previous) pop_front(); throw; }
			std::rotate(begin(), begin() + static_cast<difference_type>(count),
				begin() + static_cast<difference_type>(count + index));
		} else {
			reserve_back(count);
			try { for (T& value : values) emplace_back(std::move(value)); }
			catch (...) { while (size_ > previous) pop_back(); throw; }
			std::rotate(begin() + static_cast<difference_type>(index),
				begin() + static_cast<difference_type>(previous), end());
		}
		return begin() + static_cast<difference_type>(index);
	}

	[[no_unique_address]] Alloc allocator_;
	std::vector<block_entry> directory_;
	size_type first_ = 0;
	size_type size_ = 0;
	size_type allocated_begin_ = 0;
	size_type allocated_end_ = 0;
	pointer main_ = nullptr;
	size_type main_capacity_ = 0;
	pointer contiguous_data_ = nullptr;
};

}  // 네임스페이스 nova::containers

namespace nova_baseline {

template <typename T, typename Alloc = std::allocator<T>, std::size_t BlockBytes = 16384>
using list = containers::list<T, Alloc, BlockBytes>;

}  // 네임스페이스 nova

#endif  // NOVA_LIST_CHUNK_BASELINE_HPP_ 끝
