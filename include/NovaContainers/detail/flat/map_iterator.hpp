#ifndef NOVA_CONTAINERS_DETAIL_FLAT_MAP_ITERATOR_HPP_
#define NOVA_CONTAINERS_DETAIL_FLAT_MAP_ITERATOR_HPP_

#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace nova::containers::detail {

template <typename core_type, typename Owner, bool IsConst>
class flat_map_iterator {
 private:
	using core_iterator = std::conditional_t<IsConst,
		typename core_type::const_iterator, typename core_type::iterator>;

 public:
	using iterator_category = std::random_access_iterator_tag;
	using iterator_concept = std::random_access_iterator_tag;
	using value_type = typename core_type::value_type;
	using difference_type = std::ptrdiff_t;
	using mapped_reference =
		std::conditional_t<IsConst,
			const typename value_type::second_type&, typename value_type::second_type&>;

	struct reference {
		const typename core_type::key_type& first;
		mapped_reference second;

		operator value_type() const
			requires std::copy_constructible<typename core_type::key_type> &&
				std::copy_constructible<typename value_type::second_type> {
			return value_type(first, second);
		}
	};

	class pointer {
	 public:
		explicit pointer(reference reference) noexcept
			: reference_(reference) {}

		const reference* operator->() const noexcept { return &reference_; }

	 private:
		reference reference_;
	};

	flat_map_iterator() noexcept = default;
	flat_map_iterator(const flat_map_iterator&) noexcept = default;
	flat_map_iterator& operator=(const flat_map_iterator&) noexcept = default;

	template <bool OtherConst>
		requires (IsConst && !OtherConst)
	flat_map_iterator(const flat_map_iterator<core_type, Owner, OtherConst>& other) noexcept
		: iterator_(other.iterator_) {}

	reference operator*() const noexcept {
		return {iterator_->first, iterator_->second};
	}

	pointer operator->() const noexcept { return pointer(operator*()); }

	flat_map_iterator& operator++() noexcept {
		++iterator_;
		return *this;
	}

	flat_map_iterator operator++(int) noexcept {
		flat_map_iterator copy = *this;
		++(*this);
		return copy;
	}

	flat_map_iterator& operator--() noexcept {
		--iterator_;
		return *this;
	}

	flat_map_iterator operator--(int) noexcept {
		flat_map_iterator copy = *this;
		--(*this);
		return copy;
	}

	flat_map_iterator& operator+=(difference_type offset) noexcept {
		iterator_ += offset;
		return *this;
	}

	flat_map_iterator& operator-=(difference_type offset) noexcept {
		iterator_ -= offset;
		return *this;
	}

	flat_map_iterator operator+(difference_type offset) const noexcept {
		flat_map_iterator copy = *this;
		copy += offset;
		return copy;
	}

	flat_map_iterator operator-(difference_type offset) const noexcept {
		flat_map_iterator copy = *this;
		copy -= offset;
		return copy;
	}

	friend flat_map_iterator operator+(
		difference_type offset, flat_map_iterator iterator) noexcept {
		return iterator + offset;
	}

	reference operator[](difference_type offset) const noexcept {
		auto&& value = iterator_[offset];
		return {value.first, value.second};
	}

	template <bool OtherConst>
	difference_type operator-(
		const flat_map_iterator<core_type, Owner, OtherConst>& other) const noexcept {
		return iterator_ - other.iterator_;
	}

	template <bool OtherConst>
	bool operator==(
		const flat_map_iterator<core_type, Owner, OtherConst>& other) const noexcept {
		return iterator_ == other.iterator_;
	}

	template <bool OtherConst>
	auto operator<=>(
		const flat_map_iterator<core_type, Owner, OtherConst>& other) const noexcept {
		return iterator_ <=> other.iterator_;
	}

 private:
	explicit flat_map_iterator(core_iterator iterator) noexcept
		: iterator_(iterator) {}

	core_iterator iterator_{};
	template <typename, typename, bool>
	friend class flat_map_iterator;
	friend Owner;
};

}  // 네임스페이스 nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_FLAT_MAP_ITERATOR_HPP_
