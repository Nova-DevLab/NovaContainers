#ifndef NOVA_CONTAINERS_DETAIL_FLAT_MAP_ITERATOR_HPP_
#define NOVA_CONTAINERS_DETAIL_FLAT_MAP_ITERATOR_HPP_

#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace nova::containers::detail {

template <typename Core, typename Owner, bool IsConst>
class FlatMapIterator {
 private:
	using CoreIterator = std::conditional_t<IsConst,
		typename Core::const_iterator, typename Core::iterator>;

 public:
	using iterator_category = std::random_access_iterator_tag;
	using iterator_concept = std::random_access_iterator_tag;
	using value_type = typename Core::value_type;
	using difference_type = std::ptrdiff_t;
	using mapped_reference =
		std::conditional_t<IsConst,
			const typename value_type::second_type&, typename value_type::second_type&>;

	struct Reference {
		const typename Core::key_type& first;
		mapped_reference second;

		operator value_type() const
			requires std::copy_constructible<typename Core::key_type> &&
				std::copy_constructible<typename value_type::second_type> {
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

	FlatMapIterator() noexcept = default;
	FlatMapIterator(const FlatMapIterator&) noexcept = default;
	FlatMapIterator& operator=(const FlatMapIterator&) noexcept = default;

	template <bool OtherConst>
		requires (IsConst && !OtherConst)
	FlatMapIterator(const FlatMapIterator<Core, Owner, OtherConst>& other) noexcept
		: iterator_(other.iterator_) {}

	reference operator*() const noexcept {
		return {iterator_->first, iterator_->second};
	}

	pointer operator->() const noexcept { return pointer(operator*()); }

	FlatMapIterator& operator++() noexcept {
		++iterator_;
		return *this;
	}

	FlatMapIterator operator++(int) noexcept {
		FlatMapIterator copy = *this;
		++(*this);
		return copy;
	}

	FlatMapIterator& operator--() noexcept {
		--iterator_;
		return *this;
	}

	FlatMapIterator operator--(int) noexcept {
		FlatMapIterator copy = *this;
		--(*this);
		return copy;
	}

	FlatMapIterator& operator+=(difference_type offset) noexcept {
		iterator_ += offset;
		return *this;
	}

	FlatMapIterator& operator-=(difference_type offset) noexcept {
		iterator_ -= offset;
		return *this;
	}

	FlatMapIterator operator+(difference_type offset) const noexcept {
		FlatMapIterator copy = *this;
		copy += offset;
		return copy;
	}

	FlatMapIterator operator-(difference_type offset) const noexcept {
		FlatMapIterator copy = *this;
		copy -= offset;
		return copy;
	}

	friend FlatMapIterator operator+(
		difference_type offset, FlatMapIterator iterator) noexcept {
		return iterator + offset;
	}

	reference operator[](difference_type offset) const noexcept {
		auto&& value = iterator_[offset];
		return {value.first, value.second};
	}

	template <bool OtherConst>
	difference_type operator-(
		const FlatMapIterator<Core, Owner, OtherConst>& other) const noexcept {
		return iterator_ - other.iterator_;
	}

	template <bool OtherConst>
	bool operator==(
		const FlatMapIterator<Core, Owner, OtherConst>& other) const noexcept {
		return iterator_ == other.iterator_;
	}

	template <bool OtherConst>
	auto operator<=>(
		const FlatMapIterator<Core, Owner, OtherConst>& other) const noexcept {
		return iterator_ <=> other.iterator_;
	}

 private:
	explicit FlatMapIterator(CoreIterator iterator) noexcept
		: iterator_(iterator) {}

	CoreIterator iterator_{};
	template <typename, typename, bool>
	friend class FlatMapIterator;
	friend Owner;
};

}  // namespace nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_FLAT_MAP_ITERATOR_HPP_
