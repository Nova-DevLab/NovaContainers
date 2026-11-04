#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(NOVA_LIST_BASELINE_HEADER)
#include NOVA_LIST_BASELINE_HEADER
#endif
#if defined(NOVA_LIST_CANDIDATE_HEADER)
#include NOVA_LIST_CANDIDATE_HEADER
#else
#include "NovaContainers/list.hpp"
#endif

namespace heap_tracking {

struct allocation_header {
	void* allocation;
	std::size_t bytes;
	std::uint64_t epoch;
};
struct counters {
	std::size_t live = 0;
	std::size_t peak = 0;
	std::size_t allocations = 0;
};
inline counters state;
inline std::uint64_t epoch = 0;
inline bool enabled = false;

void* allocate(std::size_t bytes, std::size_t alignment) {
	alignment = std::max(alignment, alignof(allocation_header));
	const std::size_t actual_bytes = std::max(bytes, std::size_t{1});
	const std::size_t overhead = sizeof(allocation_header) + alignment - 1;
	if (actual_bytes > std::numeric_limits<std::size_t>::max() - overhead)
		throw std::bad_alloc();
	void* allocation = std::malloc(actual_bytes + overhead);
	if (allocation == nullptr) throw std::bad_alloc();
	const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(allocation)
		+ sizeof(allocation_header);
	const std::uintptr_t address = (start + alignment - 1) & ~(alignment - 1);
	auto* header = reinterpret_cast<allocation_header*>(address) - 1;
	header->allocation = allocation;
	header->bytes = bytes;
	header->epoch = enabled ? epoch : 0;
	if (enabled) {
		state.live += bytes;
		state.peak = std::max(state.peak, state.live);
		++state.allocations;
	}
	return reinterpret_cast<void*>(address);
}
void deallocate(void* pointer) noexcept {
	if (pointer == nullptr) return;
	auto* header = static_cast<allocation_header*>(pointer) - 1;
	if (enabled && header->epoch == epoch) state.live -= header->bytes;
	std::free(header->allocation);
}
void begin() {
	++epoch;
	state = {};
	enabled = true;
}

}  // namespace heap_tracking

#if !defined(NOVA_MEMORY_NATIVE_HEAP)
void* operator new(std::size_t bytes) {
	return heap_tracking::allocate(bytes, alignof(std::max_align_t));
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void* operator new(std::size_t bytes, std::align_val_t alignment) {
	return heap_tracking::allocate(bytes, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
	return ::operator new(bytes, alignment);
}
void operator delete(void* pointer) noexcept { heap_tracking::deallocate(pointer); }
void operator delete[](void* pointer) noexcept { heap_tracking::deallocate(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { heap_tracking::deallocate(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { heap_tracking::deallocate(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { heap_tracking::deallocate(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { heap_tracking::deallocate(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
	heap_tracking::deallocate(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
	heap_tracking::deallocate(pointer);
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
	try { return ::operator new(bytes); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
	return ::operator new(bytes, std::nothrow);
}
void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	try { return ::operator new(bytes, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	return ::operator new(bytes, alignment, std::nothrow);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { heap_tracking::deallocate(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { heap_tracking::deallocate(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
	heap_tracking::deallocate(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
	heap_tracking::deallocate(pointer);
}

#endif

namespace {

struct record64 {
	std::uint64_t key;
	std::array<std::uint64_t, 7> payload{};
};
static_assert(sizeof(record64) == 64);

std::uint64_t key_of(std::uint64_t value) { return value; }
std::uint64_t key_of(const record64& value) { return value.key; }
std::uint64_t key_of(const std::string& value) {
	std::uint64_t result = 0;
	for (unsigned char ch : value) result = result * 131 + ch;
	return result;
}

template <typename T>
T make_value(std::size_t n, bool long_strings) {
	if constexpr (std::is_same_v<T, std::string>) {
		return std::string(long_strings ? 96 : 6, static_cast<char>('a' + n % 23));
	} else if constexpr (std::is_same_v<T, record64>) {
		return {static_cast<std::uint64_t>(n)};
	} else {
		return static_cast<T>(n);
	}
}

enum class operation {
	count_front, count_middle, count_back,
	range_front, range_middle, range_back,
	input_front, input_middle, input_back,
	queue_front, queue_back,
	forward, reverse, index, sort, optimize
};
constexpr std::array operations{
	operation::count_front, operation::count_middle, operation::count_back,
	operation::range_front, operation::range_middle, operation::range_back,
	operation::input_front, operation::input_middle, operation::input_back,
	operation::queue_front, operation::queue_back,
	operation::forward, operation::reverse, operation::index, operation::sort, operation::optimize
};
constexpr std::array names{
	"count_front", "count_middle", "count_back", "range_front", "range_middle", "range_back",
	"input_front", "input_middle", "input_back", "queue_front", "queue_back", "forward", "reverse", "index", "sort", "optimize"
};
volatile std::uint64_t result_sink = 0;
std::string_view selected_group = "all";
std::string_view selected_layout = "fragmented";

#if defined(_MSC_VER)
#define NOVA_MEMORY_NOINLINE __declspec(noinline)
#else
#define NOVA_MEMORY_NOINLINE __attribute__((noinline))
#endif

template <typename List>
NOVA_MEMORY_NOINLINE std::uint64_t checksum(const List& values) {
	std::uint64_t result = 0;
	for (const auto& value : values) result = result * 17 + key_of(value);
	return result;
}

template <typename List>
struct fixture {
	using value_type = typename List::value_type;
	List values;
	std::vector<value_type> input;
	value_type fill;
	fixture(std::size_t count, operation op, bool long_strings)
		: fill(make_value<value_type>(37, long_strings)) {
		const bool queue = op == operation::queue_front || op == operation::queue_back;
		const bool insertion = op <= operation::input_back;
		const std::size_t initial = queue ? std::min(count, std::size_t{256})
			: insertion ? std::min(count, std::size_t{128}) : count;
		for (std::size_t i = 0; i < initial; ++i)
			values.push_back(make_value<value_type>(initial - i, long_strings));
		if (op >= operation::range_front && op <= operation::input_back) {
			input.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
				input.push_back(make_value<value_type>(i, long_strings));
		}
		if (selected_layout == "contiguous") values.optimize();
	}
};

// Expose the same prepared input through a single-pass iterator without a size operation.
template <typename Iterator>
struct unsized_input_iterator {
	using value_type = std::iter_value_t<Iterator>;
	using difference_type = std::ptrdiff_t;
	using iterator_concept = std::input_iterator_tag;
	using iterator_category = std::input_iterator_tag;
	Iterator current;
	decltype(auto) operator*() const { return *current; }
	unsized_input_iterator& operator++() { ++current; return *this; }
	void operator++(int) { ++current; }
	bool operator==(const unsized_input_iterator&) const = default;
};

template <operation Op, typename List>
NOVA_MEMORY_NOINLINE std::uint64_t apply_case(fixture<List>& data, std::size_t count) {
	constexpr operation op = Op;
	auto& values = data.values;
	if (op <= operation::input_back) {
		const unsigned position = static_cast<unsigned>(op) % 3;
		const std::size_t index = position == 0 ? 0 : position == 1 ? values.size() / 2 : values.size();
		const auto where = values.cbegin() + static_cast<std::ptrdiff_t>(index);
		if (op <= operation::count_back) values.insert(where, count, data.fill);
		else if (op <= operation::range_back) values.insert(where, data.input.begin(), data.input.end());
		else {
			using input_iterator = unsized_input_iterator<typename std::vector<typename List::value_type>::const_iterator>;
			values.insert(where, input_iterator{data.input.cbegin()}, input_iterator{data.input.cend()});
		}
		return 0;
	}
	if (op == operation::queue_front || op == operation::queue_back) {
		for (std::size_t i = 0; i < count; ++i) {
			if (op == operation::queue_front) {
				values.pop_back();
				values.push_front(data.fill);
			} else {
				values.pop_front();
				values.push_back(data.fill);
			}
		}
		return 0;
	}
	std::uint64_t result = 0;
	if (op == operation::forward) {
		for (const auto& value : values) result += key_of(value);
	} else if (op == operation::reverse) {
		for (auto it = values.rbegin(); it != values.rend(); ++it) result += key_of(*it);
	} else if (op == operation::index) {
		for (std::size_t i = 0; i < values.size(); ++i) result += key_of(values[i]);
	} else if (op == operation::sort) {
		std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
			return key_of(left) < key_of(right);
		});
	} else {
		values.optimize();
	}
	return result;
}

template <typename List>
std::uint64_t apply(fixture<List>& data, std::size_t count, operation op) {
	// Keep each timed operation independent of unrelated insertion code generation.
	switch (op) {
	case operation::count_front: return apply_case<operation::count_front>(data, count);
	case operation::count_middle: return apply_case<operation::count_middle>(data, count);
	case operation::count_back: return apply_case<operation::count_back>(data, count);
	case operation::range_front: return apply_case<operation::range_front>(data, count);
	case operation::range_middle: return apply_case<operation::range_middle>(data, count);
	case operation::range_back: return apply_case<operation::range_back>(data, count);
	case operation::input_front: return apply_case<operation::input_front>(data, count);
	case operation::input_middle: return apply_case<operation::input_middle>(data, count);
	case operation::input_back: return apply_case<operation::input_back>(data, count);
	case operation::queue_front: return apply_case<operation::queue_front>(data, count);
	case operation::queue_back: return apply_case<operation::queue_back>(data, count);
	case operation::forward: return apply_case<operation::forward>(data, count);
	case operation::reverse: return apply_case<operation::reverse>(data, count);
	case operation::index: return apply_case<operation::index>(data, count);
	case operation::sort: return apply_case<operation::sort>(data, count);
	case operation::optimize: return apply_case<operation::optimize>(data, count);
	}
	throw std::invalid_argument("Unknown benchmark operation");
}

struct measurement {
	double nanoseconds = 0;
	std::uint64_t checksum = 0;
	std::size_t setup_live = 0;
	std::size_t operation_peak = 0;
	std::size_t final_live = 0;
	std::size_t operation_allocations = 0;
};

template <typename List>
measurement measure_time(std::size_t count, std::size_t iterations, operation op, bool long_strings) {
	measurement result;
	for (std::size_t i = 0; i < iterations; ++i) {
		fixture<List> data(count, op, long_strings);
		std::atomic_signal_fence(std::memory_order_seq_cst);
		const auto start = std::chrono::steady_clock::now();
		const std::uint64_t sum = apply(data, count, op);
		std::atomic_signal_fence(std::memory_order_seq_cst);
		const auto finish = std::chrono::steady_clock::now();
		result.nanoseconds += std::chrono::duration<double, std::nano>(finish - start).count();
		const std::uint64_t actual = checksum(data.values) ^ sum;
		if (i != 0 && result.checksum != actual) throw std::runtime_error("Nonrepeatable checksum");
		result.checksum = actual;
		result_sink = actual;
	}
	result.nanoseconds /= static_cast<double>(iterations);
	return result;
}

template <typename List>
measurement measure_memory(std::size_t count, operation op, bool long_strings) {
	measurement result;
	heap_tracking::begin();
	{
		fixture<List> data(count, op, long_strings);
		result.setup_live = heap_tracking::state.live;
		heap_tracking::state.peak = result.setup_live;
		const std::size_t allocations_before = heap_tracking::state.allocations;
		const std::uint64_t sum = apply(data, count, op);
		result.operation_peak = heap_tracking::state.peak;
		result.final_live = heap_tracking::state.live;
		result.operation_allocations = heap_tracking::state.allocations - allocations_before;
		result.checksum = checksum(data.values) ^ sum;
	}
	const bool leaked = heap_tracking::state.live != 0;
	heap_tracking::enabled = false;
	if (leaked) throw std::runtime_error("Memory probe leaked requested bytes");
	return result;
}

template <typename T, std::size_t BlockBytes>
void run_type(const char* type, std::size_t count, std::size_t iterations, bool reverse_order,
	bool long_strings, bool queues_only) {
#if defined(NOVA_LIST_BASELINE_HEADER)
	using before_list = nova_baseline::list<T, std::allocator<T>, BlockBytes>;
#else
	using before_list = nova::list<T, std::allocator<T>, BlockBytes>;
#endif
	using after_list = nova::list<T, std::allocator<T>, BlockBytes>;
	for (std::size_t k = 0; k < operations.size(); ++k) {
		const operation op = operations[k];
		const bool queue = op == operation::queue_front || op == operation::queue_back;
		if (queue != queues_only) continue;
		if (selected_group == "count" && op > operation::count_back) continue;
		if (selected_group == "range" && (op < operation::range_front || op > operation::range_back)) continue;
		if (selected_group == "input" && (op < operation::input_front || op > operation::input_back)) continue;
		if (selected_group == "queue" && !queue) continue;
		if (selected_group == "control" && op < operation::forward) continue;
		const auto before_memory = measure_memory<before_list>(count, op, long_strings);
		const auto after_memory = measure_memory<after_list>(count, op, long_strings);
		if (before_memory.checksum != after_memory.checksum)
			throw std::runtime_error("Memory probe A/B checksum mismatch");
		// Warm up both paths before recording alternating A/B samples.
		const auto before_warmup = measure_time<before_list>(count, 1, op, long_strings);
		const auto after_warmup = measure_time<after_list>(count, 1, op, long_strings);
		if (before_warmup.checksum != before_memory.checksum || after_warmup.checksum != after_memory.checksum)
			throw std::runtime_error("Warmup checksum mismatch");
		for (std::size_t sample = 0; sample < 9; ++sample) {
			measurement before;
			measurement after;
			const bool after_first = (sample % 2 != 0) != reverse_order;
			if (after_first) {
				after = measure_time<after_list>(count, iterations, op, long_strings);
				before = measure_time<before_list>(count, iterations, op, long_strings);
			} else {
				before = measure_time<before_list>(count, iterations, op, long_strings);
				after = measure_time<after_list>(count, iterations, op, long_strings);
			}
			if (before.checksum != before_memory.checksum || after.checksum != after_memory.checksum)
				throw std::runtime_error("Timed A/B checksum mismatch");
			for (bool is_after : {false, true}) {
				const auto& timing = is_after ? after : before;
				const auto& memory = is_after ? after_memory : before_memory;
				std::cout << type << ',' << sizeof(T) << ',' << BlockBytes << ',' << selected_layout << ',' << count << ','
					<< names[k] << ',' << sample << ',' << (after_first ? "after" : "before") << ','
					<< (is_after ? "after" : "before") << ',' << timing.nanoseconds << ','
					<< memory.setup_live << ',' << memory.operation_peak << ',' << memory.final_live << ','
					<< memory.operation_allocations << ',' << timing.checksum << '\n';
			}
		}
	}
}

}  // namespace

int main(int argc, char** argv) {
	try {
		const std::size_t count = argc > 1 ? std::stoull(argv[1]) : 100000;
		const std::size_t iterations = argc > 2 ? std::stoull(argv[2]) : 3;
		const bool reverse_order = argc > 3 && std::string_view(argv[3]) == "1";
		if (argc > 4) selected_group = argv[4];
		if (argc > 5) selected_layout = argv[5];
		if (selected_layout != "fragmented" && selected_layout != "contiguous")
			throw std::invalid_argument("layout must be fragmented or contiguous");
		if (selected_group != "all" && selected_group != "count" && selected_group != "range"
			&& selected_group != "input" && selected_group != "queue" && selected_group != "control")
			throw std::invalid_argument("group must be all, count, range, input, queue or control");
		if (count == 0 || count > 10000000 || iterations == 0 || iterations > 1000)
			throw std::invalid_argument("count must be 1..10000000; iterations must be 1..1000");
#if !defined(NOVA_LIST_BASELINE_HEADER)
		std::cerr << "Self-control only: NOVA_LIST_BASELINE_HEADER is not defined.\n";
#endif
#if defined(NOVA_MEMORY_NATIVE_HEAP)
		std::cerr << "Native heap timing: global allocation wrappers disabled; memory columns are zero. ";
#else
		std::cerr << "Requested-byte probe includes directory, element blocks and string allocations; "
			"excludes malloc overhead, tracking headers and process working set. "
			"Timing disables counters but retains identical global allocation wrappers in both paths. ";

#endif
		std::cerr << "list_u64_bytes=" << sizeof(nova::list<std::uint64_t>)
			<< "; iterator_u64_bytes=" << sizeof(nova::list<std::uint64_t>::iterator) << '\n';
		std::cout << std::fixed << std::setprecision(2)
			<< "type,type_bytes,block_bytes,layout,count,operation,sample,first,implementation,ns,setup_live_bytes,"
			"operation_peak_bytes,final_live_bytes,operation_allocations,checksum\n";
		run_type<std::uint64_t, 16384>("u64", count, iterations, reverse_order, false, false);
		run_type<record64, 16384>("record64", count, iterations, reverse_order, false, false);
		run_type<std::string, 16384>("string_short", count, iterations, reverse_order, false, false);
		run_type<std::string, 16384>("string_long", count, iterations, reverse_order, true, false);
		run_type<std::uint64_t, 64>("u64", count, iterations, reverse_order, false, true);
		run_type<record64, 64>("record64", count, iterations, reverse_order, false, true);
		run_type<std::string, 64>("string_short", count, iterations, reverse_order, false, true);
		run_type<std::string, 64>("string_long", count, iterations, reverse_order, true, true);
		return 0;
	} catch (const std::exception& error) {
		heap_tracking::enabled = false;
		std::cerr << "List memory benchmark failed: " << error.what() << '\n';
		return 1;
	}
}
