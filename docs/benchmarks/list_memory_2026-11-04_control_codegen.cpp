#include <algorithm>
#include <cstdint>

#include NOVA_LIST_CODEGEN_HEADER

using list_type = nova::list<std::uint64_t>;

extern "C" __declspec(noinline) std::uint64_t forward_sum(const list_type& values) {
	std::uint64_t sum = 0;
	for (std::uint64_t value : values) sum += value;
	return sum;
}
extern "C" __declspec(noinline) std::uint64_t reverse_sum(const list_type& values) {
	std::uint64_t sum = 0;
	for (auto it = values.rbegin(); it != values.rend(); ++it) sum += *it;
	return sum;
}
extern "C" __declspec(noinline) std::uint64_t index_sum(const list_type& values) {
	std::uint64_t sum = 0;
	for (std::size_t i = 0; i < values.size(); ++i) sum += values[i];
	return sum;
}
extern "C" __declspec(noinline) void sort_values(list_type& values) {
	std::sort(values.begin(), values.end());
}
extern "C" __declspec(noinline) void optimize_values(list_type& values) {
	values.optimize();
}
extern "C" __declspec(noinline) void queue_back(list_type& values, std::uint64_t value) {
	values.pop_front();
	values.push_back(value);
}
extern "C" __declspec(noinline) void queue_front(list_type& values, std::uint64_t value) {
	values.pop_back();
	values.push_front(value);
}
