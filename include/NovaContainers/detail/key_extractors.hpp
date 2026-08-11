#ifndef NOVA_CONTAINERS_DETAIL_KEY_EXTRACTORS_HPP_
#define NOVA_CONTAINERS_DETAIL_KEY_EXTRACTORS_HPP_

#include <utility>

namespace nova::containers::detail {

struct PairFirstKeyOfValue {
	template <typename First, typename Second>
	constexpr const First& operator()(
		const std::pair<First, Second>& value) const noexcept {
		return value.first;
	}
};

struct IdentityKeyOfValue {
	template <typename Value>
	constexpr const Value& operator()(const Value& value) const noexcept {
		return value;
	}
};

}  // namespace nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_KEY_EXTRACTORS_HPP_
