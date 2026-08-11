#ifndef NOVA_CONTAINERS_DETAIL_CONCEPTS_HPP_
#define NOVA_CONTAINERS_DETAIL_CONCEPTS_HPP_

#include <concepts>
#include <cstddef>
#include <functional>
#include <utility>

namespace nova::containers::detail {

template <typename H, typename K>
concept Hashable = requires(const H& hash, const K& key) {
  { hash(key) } -> std::convertible_to<std::size_t>;
};

template <typename K>
concept HashableWithStd = requires(const K& k) {
  { std::hash<K>{}(k) } -> std::convertible_to<std::size_t>;
};

template <typename Key, typename Eq>
concept KeyEquality = requires(const Eq& eq, const Key& a, const Key& b) {
  { eq(a, b) } -> std::convertible_to<bool>;
};

template <typename Eq, typename Left, typename Right>
concept KeyEqualityWith = requires(const Eq& eq, const Left& left,
								   const Right& right) {
	{ eq(left, right) } -> std::convertible_to<bool>;
};

template <typename T>
concept Transparent = requires {
	typename T::is_transparent;
};

template <typename Compare, typename Left, typename Right>
concept KeyOrderedWith = requires(const Compare& compare, const Left& left,
								  const Right& right) {
	{ compare(left, right) } -> std::convertible_to<bool>;
	{ compare(right, left) } -> std::convertible_to<bool>;
};

}  // namespace nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_CONCEPTS_HPP_
