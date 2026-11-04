#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "NovaContainers/list.hpp"

#if defined(_MSC_VER)
#define NOVA_OPTIMIZE_NOINLINE __declspec(noinline)
#else
#define NOVA_OPTIMIZE_NOINLINE __attribute__((noinline))
#endif

namespace {

using benchmark_clock = std::chrono::steady_clock;
constexpr std::size_t k_samples = 5;
volatile std::uint64_t result_sink = 0;
std::size_t measurement_order = 0;

struct record {
	std::uint64_t key = 0;
	std::array<std::uint64_t, 7> payload{};
};
static_assert(sizeof(record) == 64);

std::uint64_t key_of(std::uint64_t value) { return value; }
std::uint64_t key_of(const record& value) { return value.key; }

template <typename T>
T make_value(std::size_t index) {
	const std::uint64_t value =
		(static_cast<std::uint64_t>(index) * 6364136223846793005ULL) ^ (index >> 3);
	if constexpr (std::is_same_v<T, std::uint64_t>) return value;
	else return record{value, {}};
}

template <typename Container>
NOVA_OPTIMIZE_NOINLINE std::uint64_t sum_range(const Container& values) {
	std::uint64_t result = 0;
	for (const auto& value : values) result += key_of(value);
	return result;
}

template <typename Container>
NOVA_OPTIMIZE_NOINLINE std::uint64_t sum_index(const Container& values) {
	std::uint64_t result = 0;
	for (std::size_t index = 0; index < values.size(); ++index) result += key_of(values[index]);
	return result;
}

template <typename Container>
NOVA_OPTIMIZE_NOINLINE std::uint64_t sum_random(
	const Container& values, const std::vector<std::size_t>& indices) {
	std::uint64_t result = 0;
	for (std::size_t index : indices) result += key_of(values[index]);
	return result;
}

template <typename T>
NOVA_OPTIMIZE_NOINLINE std::uint64_t sum_span(std::span<const T> values) {
	std::uint64_t result = 0;
	for (const T& value : values) result += key_of(value);
	return result;
}

template <typename T>
NOVA_OPTIMIZE_NOINLINE std::uint64_t sum_for_each(const nova::list<T>& values) {
	std::uint64_t result = 0;
	values.for_each([&](const T& value) { result += key_of(value); });
	return result;
}

template <typename T>
nova::list<T> make_segmented(const std::vector<T>& input) {
	nova::list<T> values;
	values.reserve_back(input.size());
	for (std::size_t index = 1; index < input.size(); ++index) values.emplace_back(input[index]);
	values.emplace_front(input.front());
	if (values.is_contiguous()) throw std::runtime_error("Segmented fixture is contiguous");
	return values;
}

template <typename T>
void add_edges(nova::list<T>& values, const std::vector<T>& front, const std::vector<T>& back) {
	for (auto it = front.rbegin(); it != front.rend(); ++it) values.emplace_front(*it);
	for (const T& value : back) values.emplace_back(value);
	if (values.is_contiguous()) throw std::runtime_error("Mixed fixture is contiguous");
}

struct row {
	const char* type;
	const char* state;
	const char* container;
	const char* operation;
	std::size_t count;
	std::size_t visits;
	std::size_t iterations;
	bool contiguous;
};

void output(const row& row, const std::string& sample, double elapsed, double setup,
	std::uint64_t checksum) {
	std::cout << row.type << ',' << row.state << ',' << measurement_order << ',' << row.container << ','
		<< row.operation << ',' << row.count << ',' << row.visits << ','
		<< row.iterations << ',' << (row.contiguous ? 1 : 0) << ',' << sample << ','
		<< std::fixed << std::setprecision(2) << elapsed << ',' << setup << ','
		<< checksum << '\n';
}

void output_median(const row& row, std::array<double, k_samples> elapsed,
	std::array<double, k_samples> setup, std::uint64_t checksum) {
	std::sort(elapsed.begin(), elapsed.end());
	std::sort(setup.begin(), setup.end());
	output(row, "median", elapsed[k_samples / 2], setup[k_samples / 2], checksum);
}

struct read_case {
	row row;
	std::uint64_t expected;
	std::function<std::uint64_t()> execute;
	std::array<double, k_samples> elapsed{};
};

void measure_reads(std::vector<read_case>& cases, bool reverse_order) {
	for (auto& entry : cases) {
		if (entry.execute() != entry.expected) throw std::runtime_error("Warmup checksum mismatch");
	}
	for (std::size_t sample = 0; sample < k_samples; ++sample) {
		for (std::size_t position = 0; position < cases.size(); ++position) {
			const std::size_t offset = reverse_order ? cases.size() - 1 - position : position;
			auto& entry = cases[(offset + sample) % cases.size()];
			std::uint64_t checksum = 0;
			std::atomic_signal_fence(std::memory_order_seq_cst);
			const auto begin = benchmark_clock::now();
			for (std::size_t iteration = 0; iteration < entry.row.iterations; ++iteration) {
				checksum += entry.execute();
				std::atomic_signal_fence(std::memory_order_seq_cst);
			}
			const auto finish = benchmark_clock::now();
			result_sink = checksum;
			if (checksum != entry.expected * entry.row.iterations) {
				throw std::runtime_error("Timed checksum mismatch");
			}
			entry.elapsed[sample] =
				std::chrono::duration<double, std::nano>(finish - begin).count()
				/ static_cast<double>(entry.row.iterations);
		}
	}
	for (const auto& entry : cases) {
		for (std::size_t sample = 0; sample < k_samples; ++sample) {
			output(entry.row, std::to_string(sample), entry.elapsed[sample], 0, entry.expected);
		}
		output_median(entry.row, entry.elapsed, {}, entry.expected);
	}
}

template <typename Container>
void add_read_cases(std::vector<read_case>& cases, const char* type, const char* state,
	const char* name, const Container& values, const std::vector<std::size_t>& indices,
	std::size_t iterations, bool contiguous) {
	const std::uint64_t expected = sum_range(values);
	const std::uint64_t random_expected = sum_random(values, indices);
	cases.push_back({{type, state, name, "range_for", values.size(), values.size(),
		iterations, contiguous}, expected, [&] { return sum_range(values); }});
	cases.push_back({{type, state, name, "index_for", values.size(), values.size(),
		iterations, contiguous}, expected, [&] { return sum_index(values); }});
	cases.push_back({{type, state, name, "random_lookup", values.size(), indices.size(),
		std::min(iterations, std::size_t{5}), contiguous}, random_expected,
		[&] { return sum_random(values, indices); }});
}

template <typename Factory>
void measure_optimize(const char* type, const char* state, const std::vector<typename
	std::invoke_result_t<Factory>::value_type>& expected, Factory factory) {
	using list_type = std::invoke_result_t<Factory>;
	std::array<double, k_samples> elapsed{}, setup{};
	const std::uint64_t checksum = sum_range(expected);
	for (std::size_t sample = 0; sample < k_samples; ++sample) {
		const auto setup_begin = benchmark_clock::now();
		list_type snapshot = factory();
		const auto setup_end = benchmark_clock::now();
		std::atomic_signal_fence(std::memory_order_seq_cst);
		const auto begin = benchmark_clock::now();
		snapshot.optimize();
		const auto finish = benchmark_clock::now();
		std::atomic_signal_fence(std::memory_order_seq_cst);
		elapsed[sample] = std::chrono::duration<double, std::nano>(finish - begin).count();
		setup[sample] = std::chrono::duration<double, std::nano>(setup_end - setup_begin).count();
		if (snapshot.size() != expected.size() || !snapshot.is_contiguous() || sum_range(snapshot) != checksum
			|| !std::equal(snapshot.begin(), snapshot.end(), expected.begin(),
				[](const auto& left, const auto& right) { return key_of(left) == key_of(right); })) {
			throw std::runtime_error("Optimize changed logical sequence");
		}
		result_sink = checksum;
		output({type, state, "nova_list", "optimize_only", expected.size(),
			expected.size(), 1, true}, std::to_string(sample), elapsed[sample], setup[sample], checksum);
	}
	output_median({type, state, "nova_list", "optimize_only", expected.size(),
		expected.size(), 1, true}, elapsed, setup, checksum);
}

template <typename T>
void measure_copy(const char* type, const char* state, const nova::list<T>& source) {
	const std::uint64_t expected = sum_range(source);
	std::array<double, k_samples> elapsed{};
	bool copied_contiguous = false;
	for (std::size_t sample = 0; sample < k_samples; ++sample) {
		std::atomic_signal_fence(std::memory_order_seq_cst);
		const auto begin = benchmark_clock::now();
		nova::list<T> snapshot(source);
		const auto finish = benchmark_clock::now();
		elapsed[sample] = std::chrono::duration<double, std::nano>(finish - begin).count();
		if (sum_range(snapshot) != expected) throw std::runtime_error("Copy checksum mismatch");
		copied_contiguous = snapshot.is_contiguous();
		result_sink = expected;
		output({type, state, "nova_list", "copy_construct_only", source.size(), source.size(),
			1, snapshot.is_contiguous()}, std::to_string(sample), elapsed[sample], 0, expected);
	}
	output_median({type, state, "nova_list", "copy_construct_only", source.size(), source.size(),
		1, copied_contiguous}, elapsed, {}, expected);
}

template <typename Factory>
void measure_sort(const char* type, const char* state, const char* name, Factory factory) {
	using container_type = std::invoke_result_t<Factory>;
	std::array<double, k_samples> elapsed{}, setup{};
	std::size_t count = 0;
	std::uint64_t expected = 0;
	bool contiguous = false;
	for (std::size_t sample = 0; sample < k_samples; ++sample) {
		const auto setup_begin = benchmark_clock::now();
		container_type values = factory();
		const auto setup_end = benchmark_clock::now();
		count = values.size();
		expected = sum_range(values);
		if constexpr (requires { values.is_contiguous(); }) contiguous = values.is_contiguous();
		else contiguous = requires { values.data(); };
		auto compare = [](const auto& left, const auto& right) { return key_of(left) < key_of(right); };
		std::atomic_signal_fence(std::memory_order_seq_cst);
		const auto begin = benchmark_clock::now();
		std::sort(values.begin(), values.end(), compare);
		const auto finish = benchmark_clock::now();
		elapsed[sample] = std::chrono::duration<double, std::nano>(finish - begin).count();
		setup[sample] = std::chrono::duration<double, std::nano>(setup_end - setup_begin).count();
		if (!std::is_sorted(values.begin(), values.end(), compare) || sum_range(values) != expected) {
			throw std::runtime_error("Sort validation failed");
		}
		result_sink = expected;
		output({type, state, name, "sort_only", count, count, 1, contiguous},
			std::to_string(sample), elapsed[sample], setup[sample], expected);
	}
	output_median({type, state, name, "sort_only", count, count, 1, contiguous},
		elapsed, setup, expected);
}

template <typename T, typename Factory>
void run_state(const char* type, const char* state, const std::vector<T>& expected,
	const std::deque<T>& deque, Factory factory, std::size_t iterations,
	bool reverse_order, bool measure_sort_enabled) {
	nova::list<T> values = factory();
	const std::uint64_t checksum = sum_range(expected);
	if (sum_range(values) != checksum || sum_index(values) != checksum || sum_range(deque) != checksum) {
		throw std::runtime_error("State fixture checksum mismatch");
	}
	std::vector<std::size_t> indices(std::min(expected.size(), std::size_t{100000}));
	std::uint64_t random = 0x4E4F5641;
	for (std::size_t& index : indices) {
		random ^= random << 13;
		random ^= random >> 7;
		random ^= random << 17;
		index = static_cast<std::size_t>(random % expected.size());
	}
	std::vector<read_case> cases;
	add_read_cases(cases, type, state, "vector", expected, indices, iterations, true);
	add_read_cases(cases, type, state, "deque", deque, indices, iterations, false);
	add_read_cases(cases, type, state, "nova_list", values, indices, iterations, values.is_contiguous());
	const std::span<const T> vector_view(expected);
	cases.push_back({{type, state, "vector", "contiguous_view", expected.size(), expected.size(),
		iterations, true}, checksum, [&] { return sum_span(vector_view); }});
	if (values.is_contiguous()) {
		const std::span<const T> view = std::as_const(values).contiguous_view();
		cases.push_back({{type, state, "nova_list", "contiguous_view", values.size(), values.size(),
			iterations, true}, checksum, [view] { return sum_span(view); }});
	}
	cases.push_back({{type, state, "nova_list", "for_each", values.size(), values.size(),
		iterations, values.is_contiguous()}, checksum, [&] { return sum_for_each(values); }});
	measure_reads(cases, reverse_order);
	measure_copy(type, state, values);
	if (measure_sort_enabled) {
		measure_sort(type, state, "nova_list", factory);
		measure_sort(type, state, "vector", [&] { return expected; });
		measure_sort(type, state, "deque", [&] { return deque; });
	}
}

template <typename T>
void run(const char* type, std::size_t count, std::size_t iterations,
	bool reverse_order, bool measure_sort_enabled) {
	std::vector<T> input;
	input.reserve(count);
	for (std::size_t index = 0; index < count; ++index) input.emplace_back(make_value<T>(index));
	std::deque<T> deque(input.begin(), input.end());
	auto segmented = [&] { return make_segmented(input); };
	auto optimized = [&] {
		auto values = segmented();
		values.optimize();
		if (!values.is_contiguous()) throw std::runtime_error("Optimize did not make storage contiguous");
		return values;
	};
	run_state(type, "segmented", input, deque, segmented, iterations, reverse_order, measure_sort_enabled);
	measure_optimize(type, "segmented", input, segmented);
	run_state(type, "optimized", input, deque, optimized, iterations, reverse_order, measure_sort_enabled);
	std::deque<T>{}.swap(deque);

	const std::size_t edge_count =
		std::max(nova::list<T>::k_block_capacity + 3, std::min(count / 100, std::size_t{16384}));
	std::vector<T> front, back;
	front.reserve(edge_count);
	back.reserve(edge_count);
	for (std::size_t index = 0; index < edge_count; ++index) {
		front.emplace_back(make_value<T>(count + index));
		back.emplace_back(make_value<T>(count + edge_count + index));
	}
	std::vector<T> mixed_input;
	mixed_input.reserve(count + 2 * edge_count);
	mixed_input.insert(mixed_input.end(), front.begin(), front.end());
	mixed_input.insert(mixed_input.end(), input.begin(), input.end());
	mixed_input.insert(mixed_input.end(), back.begin(), back.end());
	std::deque<T> mixed_deque(mixed_input.begin(), mixed_input.end());
	auto mixed = [&] {
		auto values = optimized();
		add_edges(values, front, back);
		return values;
	};
	auto reoptimized = [&] {
		auto values = mixed();
		values.optimize();
		if (!values.is_contiguous()) throw std::runtime_error("Reoptimize did not make storage contiguous");
		return values;
	};
	run_state(type, "mixed", mixed_input, mixed_deque, mixed, iterations, reverse_order, measure_sort_enabled);
	measure_optimize(type, "mixed", mixed_input, mixed);
	run_state(type, "reoptimized", mixed_input, mixed_deque, reoptimized,
		iterations, reverse_order, measure_sort_enabled);
}

}  // namespace

int main(int argc, char** argv) {
	try {
		const std::size_t count = argc > 1 ? std::stoull(argv[1]) : 100000;
		const std::size_t iterations = argc > 2 ? std::stoull(argv[2]) : 30;
		const bool reverse_order = argc > 3 && std::stoull(argv[3]) != 0;
		const bool measure_sort_enabled = argc > 4 && std::stoull(argv[4]) != 0;
		if (argc > 5 || count < 2 || iterations == 0
			|| count > static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max()) / 4) {
			throw std::invalid_argument(
				"usage: optimize_benchmark [count>=2] [iterations>=1] [reverse_order=0|1] [sort=0|1]");
		}
		measurement_order = reverse_order ? 1 : 0;
		std::cout << "type,state,measurement_order,container,operation,count,visits,iterations,is_contiguous,sample,elapsed_ns,setup_ns,checksum\n";
		run<std::uint64_t>("u64", count, iterations, reverse_order, measure_sort_enabled);
		run<record>("record64", count, iterations, reverse_order, measure_sort_enabled);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "Optimize benchmark failure: " << error.what() << '\n';
		return 1;
	}
}
