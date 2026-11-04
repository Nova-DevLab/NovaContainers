#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "NovaContainers/list.hpp"

#if defined(_MSC_VER)
#define NOVA_BENCH_NOINLINE __declspec(noinline)
#else
#define NOVA_BENCH_NOINLINE __attribute__((noinline))
#endif

namespace {

using benchmark_clock = std::chrono::steady_clock;
volatile std::uint64_t result_sink = 0;

struct record {
	std::uint64_t key;
	std::uint64_t value;
	std::array<std::uint64_t, 6> payload{};
	friend bool operator<(const record& left, const record& right) noexcept {
		return left.key < right.key;
	}
};

static_assert(sizeof(record) == 64);

std::uint64_t key_of(std::uint64_t value) { return value; }
std::uint64_t key_of(const record& value) { return value.key; }

template <typename T>
T make_value(std::uint64_t value) {
	if constexpr (std::is_same_v<T, record>) {
		return record{value, value ^ 0x12345678, {}};
	} else {
		return value;
	}
}

template <typename Container>
NOVA_BENCH_NOINLINE std::uint64_t sum_iterator(const Container& values) {
	std::uint64_t sum = 0;
	for (const auto& value : values) {
		sum += key_of(value);
	}
	return sum;
}

template <typename Container>
NOVA_BENCH_NOINLINE std::uint64_t sum_index(const Container& values) {
	std::uint64_t sum = 0;
	for (std::size_t i = 0; i < values.size(); ++i) {
		sum += key_of(values[i]);
	}
	return sum;
}

template <typename T>
NOVA_BENCH_NOINLINE std::uint64_t sum_blocks(const nova::list<T>& values) {
	std::uint64_t sum = 0;
	for (auto block : values.blocks()) {
		for (const T& value : block) {
			sum += key_of(value);
		}
	}
	return sum;
}

template <typename Container>
NOVA_BENCH_NOINLINE std::uint64_t sum_random(
	const Container& values, const std::vector<std::size_t>& indices) {
	std::uint64_t sum = 0;
	for (std::size_t index : indices) {
		sum += key_of(values[index]);
	}
	return sum;
}

template <typename Function>
void measure(const char* type, const char* container, const char* operation,
	std::size_t count, std::size_t iterations, Function&& function) {
	std::array<double, 5> samples{};
	result_sink = function();
	for (double& sample : samples) {
		std::uint64_t checksum = 0;
		std::atomic_signal_fence(std::memory_order_seq_cst);
		const auto start = benchmark_clock::now();
		for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
			checksum += function();
			std::atomic_signal_fence(std::memory_order_seq_cst);
		}
		const auto stop = benchmark_clock::now();
		result_sink = checksum;
		sample = std::chrono::duration<double, std::nano>(stop - start).count()
			/ static_cast<double>(iterations);
	}
	std::sort(samples.begin(), samples.end());
	std::cout << type << ',' << container << ',' << operation << ',' << count << ','
		<< iterations << ',' << std::fixed << std::setprecision(2) << samples[2] << ','
		<< samples[2] / static_cast<double>(count) << ',' << result_sink << '\n';
}

template <typename Container>
void measure_read(const char* type, const char* name, const Container& values,
	const std::vector<std::size_t>& indices, std::size_t iterations) {
	measure(type, name, "iterate", values.size(), iterations,
		[&] { return sum_iterator(values); });
	measure(type, name, "index", values.size(), iterations,
		[&] { return sum_index(values); });
	measure(type, name, "random", values.size(), iterations,
		[&] { return sum_random(values, indices); });
}

template <typename Container>
void measure_append(const char* type, const char* name, std::size_t count,
	std::size_t iterations, bool reserve) {
	measure(type, name, "append_build_destroy", count, iterations, [=] {
		Container values;
		if (reserve) {
			if constexpr (requires { values.reserve(count); }) {
				values.reserve(count);
			} else if constexpr (requires { values.reserve_back(count); }) {
				values.reserve_back(count);
			}
		}
		for (std::size_t i = 0; i < count; ++i) {
			values.emplace_back(make_value<typename Container::value_type>(i));
		}
		return key_of(values.front()) + key_of(values.back()) + values.size();
	});
}

template <typename Container>
void measure_both_ends(const char* type, const char* name, std::size_t count,
	std::size_t iterations) {
	measure(type, name, "both_ends_build_destroy", count, iterations, [=] {
		Container values;
		for (std::size_t i = 0; i < count; ++i) {
			auto value = make_value<typename Container::value_type>(i);
			if ((i & 1) == 0) {
				if constexpr (requires { values.emplace_front(std::move(value)); }) {
					values.emplace_front(std::move(value));
				} else {
					values.emplace(values.begin(), std::move(value));
				}
			} else {
				values.emplace_back(std::move(value));
			}
		}
		return key_of(values.front()) + key_of(values.back()) + values.size();
	});
}

template <typename Container>
void measure_middle(const char* type, const char* name, const Container& source,
	std::size_t iterations) {
	measure(type, name, "middle_100_pairs_copy_destroy", source.size(), iterations, [&] {
		Container values(source);
		for (int i = 0; i < 100; ++i) {
			const auto position = static_cast<typename Container::difference_type>(values.size() / 2);
			values.emplace(values.begin() + position, make_value<typename Container::value_type>(i));
			values.erase(values.begin() + position);
		}
		return key_of(values.front()) + key_of(values.back()) + values.size();
	});
}

template <typename Container>
void measure_sort(const char* type, const char* name, const Container& source,
	std::size_t iterations) {
	measure(type, name, "sort_copy_destroy", source.size(), iterations, [&] {
		Container values(source);
		std::sort(values.begin(), values.end());
		if (!std::is_sorted(values.begin(), values.end())) {
			throw std::runtime_error("sort verification failed");
		}
		return key_of(values.front()) + key_of(values.back()) + values.size();
	});
}

template <typename T>
void run(const char* type, std::size_t count, std::size_t iterations) {
	std::vector<T> vector;
	vector.reserve(count);
	std::mt19937_64 random(0x4E4F5641);
	for (std::size_t i = 0; i < count; ++i) {
		vector.emplace_back(make_value<T>(random() % 100000000));
	}
	std::deque<T> deque(vector.begin(), vector.end());
	nova::list<T> list(vector.begin(), vector.end());
	std::vector<std::size_t> indices(count);
	for (std::size_t& index : indices) {
		index = static_cast<std::size_t>(random() % count);
	}
	const std::uint64_t expected = sum_iterator(vector);
	if (sum_iterator(deque) != expected || sum_iterator(list) != expected
		|| sum_index(list) != expected || sum_blocks(list) != expected
		|| sum_random(list, indices) != sum_random(vector, indices)) {
		throw std::runtime_error("benchmark checksum mismatch");
	}
	measure_read(type, "vector", vector, indices, iterations);
	measure_read(type, "deque", deque, indices, iterations);
	measure_read(type, "nova_list", list, indices, iterations);
	measure(type, "nova_list", "blocks", count, iterations, [&] { return sum_blocks(list); });
	const std::size_t build_iterations = (std::max)(std::size_t{1}, iterations / 5);
	measure_append<std::vector<T>>(type, "vector", count, build_iterations, false);
	measure_append<std::vector<T>>(type, "vector_reserved", count, build_iterations, true);
	measure_append<std::deque<T>>(type, "deque", count, build_iterations, false);
	measure_append<nova::list<T>>(type, "nova_list", count, build_iterations, false);
	measure_append<nova::list<T>>(type, "nova_list_reserved", count, build_iterations, true);
	const std::size_t both_count = (std::min)(count, std::size_t{10000});
	measure_both_ends<std::vector<T>>(type, "vector", both_count, 1);
	measure_both_ends<std::deque<T>>(type, "deque", both_count, build_iterations);
	measure_both_ends<nova::list<T>>(type, "nova_list", both_count, build_iterations);
	measure_middle(type, "vector", vector, 1);
	measure_middle(type, "deque", deque, 1);
	measure_middle(type, "nova_list", list, 1);
	measure_sort(type, "vector", vector, 1);
	measure_sort(type, "deque", deque, 1);
	measure_sort(type, "nova_list", list, 1);
}

}  // namespace

int main(int argc, char** argv) {
	try {
		const std::size_t count = argc > 1 ? std::stoull(argv[1]) : 100000;
		const std::size_t iterations = argc > 2 ? std::stoull(argv[2]) : 20;
		if (count < 2 || iterations == 0 || argc > 3) {
			throw std::invalid_argument("usage: nova_containers_list_benchmark [count>=2] [iterations>=1]");
		}
		std::cout << "type,container,operation,count,iterations,median_ns,ns_per_element,checksum\n";
		run<std::uint64_t>("u64", count, iterations);
		run<record>("record64", count, iterations);
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "List benchmark failure: " << error.what() << '\n';
		return 1;
	}
}
