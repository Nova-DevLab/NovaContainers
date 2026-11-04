#ifndef NOVA_CONTAINERS_DETAIL_CONCEPTS_HPP_
#define NOVA_CONTAINERS_DETAIL_CONCEPTS_HPP_

#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace nova::containers::detail {

template <typename H, typename K>
concept hashable = requires(const H& hash, const K& key) {
	{ hash(key) } -> std::convertible_to<std::size_t>;
};

template <typename K>
concept hashable_with_std = requires(const K& k) {
	{ std::hash<K>{}(k) } -> std::convertible_to<std::size_t>;
};

template <typename Key, typename Eq>
concept key_equality = requires(const Eq& eq, const Key& a, const Key& b) {
	{ eq(a, b) } -> std::convertible_to<bool>;
};

template <typename Eq, typename Left, typename Right>
concept key_equality_with = requires(const Eq& eq, const Left& left,
								   const Right& right) {
	{ eq(left, right) } -> std::convertible_to<bool>;
};

template <typename T>
concept transparent = requires {
	typename T::is_transparent;
};

template <typename Compare, typename Left, typename Right>
concept key_ordered_with = requires(const Compare& compare, const Left& left,
								  const Right& right) {
	{ compare(left, right) } -> std::convertible_to<bool>;
	{ compare(right, left) } -> std::convertible_to<bool>;
};

template <typename Hash, typename Eq, typename Key, typename LookupKey>
concept heterogeneous_hash_lookup =
	!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
	transparent<Hash> && transparent<Eq> && hashable<Hash, LookupKey> &&
	key_equality_with<Eq, Key, LookupKey>;

template <typename Compare, typename Key, typename LookupKey>
concept heterogeneous_ordered_lookup =
	!std::same_as<std::remove_cvref_t<LookupKey>, Key> &&
	transparent<Compare> && key_ordered_with<Compare, Key, LookupKey>;

}  // 네임스페이스 nova::containers::detail

#endif  // NOVA_CONTAINERS_DETAIL_CONCEPTS_HPP_
