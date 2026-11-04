#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#if defined(NOVA_LIST_BASELINE_HEADER)
#include NOVA_LIST_BASELINE_HEADER
#endif
#include "NovaContainers/list.hpp"

#if defined(_MSC_VER)
#define NOVA_ITERATION_NOINLINE __declspec(noinline)
#else
#define NOVA_ITERATION_NOINLINE __attribute__((noinline))
#endif

namespace {

struct record {
	std::uint64_t key = 0;
	std::array<std::uint64_t, 7> payload{};
};
static_assert(sizeof(record) == 64);

std::uint64_t key_of(std::uint64_t value) { return value; }
std::uint64_t key_of(const record& value) { return value.key; }

template <typename Container>
NOVA_ITERATION_NOINLINE std::uint64_t sum_values(const Container& values) {
	std::uint64_t sum = 0;
	for (const auto& value : values) sum += key_of(value);
	return sum;
}

// 2026-11-04 i7-1165G7/MSVC Release: 10만 uint64_t 합산에서 일반 순회
// 63.12~67.92 us, for_each 55.29~55.44 us. 조건과 원본은 docs/list_iteration_optimization.md에 기록한다.
template <typename Container>
NOVA_ITERATION_NOINLINE std::uint64_t sum_for_each(const Container& values) {
	if constexpr (requires { values.for_each([](const typename Container::value_type&) {}); }) {
		std::uint64_t sum = 0;
		values.for_each([&](const auto& value) { sum += key_of(value); });
		return sum;
	} else {
		return sum_values(values);
	}
}

template <typename Container>
NOVA_ITERATION_NOINLINE std::uint64_t reverse_sum(const Container& values) {
	std::uint64_t sum = 0;
	for (auto it = values.rbegin(); it != values.rend(); ++it) sum += key_of(*it);
	return sum;
}

template <typename Container>
NOVA_ITERATION_NOINLINE std::uint64_t count_matching(const Container& values) {
	std::uint64_t count = 0;
	for (const auto& value : values) {
		if ((key_of(value) & 7) == 0) ++count;
	}
	return count;
}

volatile std::uint64_t result_sink = 0;

template <typename Function>
double time(std::size_t iterations, Function function) {
	std::uint64_t checksum = 0;
	const auto start = std::chrono::steady_clock::now();
	for (std::size_t n = 0; n < iterations; ++n) {
		std::atomic_signal_fence(std::memory_order_seq_cst);
		checksum += function();
	}
	result_sink = checksum;
	return std::chrono::duration<double, std::nano>(
		std::chrono::steady_clock::now() - start).count()
		/ static_cast<double>(iterations);
}

struct sample {
	double before;
	double after;
	double vector;
};

template <typename Before, typename After, typename T, typename Operation>
void measure(const char* type, const char* operation, const char* reference,
	const Before& before, const After& after, const std::vector<T>& vector,
	std::size_t iterations, bool reverse, Operation apply) {
	const std::uint64_t expected = apply(vector);
	if (apply(before) != expected || apply(after) != expected) {
		throw std::runtime_error("Iteration checksum mismatch");
	}
	std::array<sample, 11> samples{};
	for (std::size_t n = 0; n < samples.size(); ++n) {
		auto& sample = samples[n];
		if (((n & 1) != 0) == reverse) {
			sample.before = time(iterations, [&] { return apply(before); });
			sample.after = time(iterations, [&] { return apply(after); });
		} else {
			sample.after = time(iterations, [&] { return apply(after); });
			sample.before = time(iterations, [&] { return apply(before); });
		}
		sample.vector = time(iterations, [&] { return apply(vector); });
	}
	auto output = [&](const std::string& sample, double previous, double current, double contiguous) {
		std::cout << type << ',' << before.size() << ',' << operation << ','
			<< (reverse ? "after_first" : "before_first") << ',' << reference << ','
			<< sample << ',' << previous << ',' << current << ',' << contiguous << ','
			<< sizeof(typename Before::const_iterator) << ','
			<< sizeof(typename After::const_iterator) << ',' << expected << '\n';
	};
	std::array<double, 11> b{}, a{}, v{};
	for (std::size_t n = 0; n < samples.size(); ++n) {
		output(std::to_string(n), samples[n].before, samples[n].after, samples[n].vector);
		b[n] = samples[n].before;
		a[n] = samples[n].after;
		v[n] = samples[n].vector;
	}
	std::sort(b.begin(), b.end());
	std::sort(a.begin(), a.end());
	std::sort(v.begin(), v.end());
	output("median", b[5], a[5], v[5]);
}

template <typename T>
void run(std::size_t count, std::size_t iterations, bool reverse, const char* type) {
	std::vector<T> input(count);
	for (std::size_t n = 0; n < count; ++n) {
		const std::uint64_t value = (n * 6364136223846793005ULL) ^ (n >> 3);
		if constexpr (std::is_same_v<T, std::uint64_t>) input[n] = value;
		else input[n].key = value;
	}
#if defined(NOVA_LIST_BASELINE_HEADER)
	using before_type = nova_baseline::list<T>;
	constexpr const char* reference = "before_list";
#else
	using before_type = std::deque<T>;
	constexpr const char* reference = "deque";
#endif
	before_type before;
	nova::list<T> after;
	if (reverse) {
		after.assign(input.begin(), input.end());
		before.assign(input.begin(), input.end());
	} else {
		before.assign(input.begin(), input.end());
		after.assign(input.begin(), input.end());
	}
	measure(type, "forward_sum", reference, before, after, input, iterations, reverse,
		[](const auto& values) { return sum_values(values); });
	measure(type, "for_each_sum", reference, before, after, input, iterations, reverse,
		[](const auto& values) { return sum_for_each(values); });
	measure(type, "reverse_sum", reference, before, after, input, iterations, reverse,
		[](const auto& values) { return reverse_sum(values); });
	measure(type, "count", reference, before, after, input, iterations, reverse,
		[](const auto& values) { return count_matching(values); });
}

}  // namespace

int main(int argc, char** argv) {
	try {
		const std::size_t count = argc > 1 ? std::stoull(argv[1]) : 100000;
		const std::size_t iterations = argc > 2 ? std::stoull(argv[2]) : 100;
		const bool reverse = argc > 3 && std::stoull(argv[3]) != 0;
		if (argc > 4 || count == 0 || iterations == 0) {
			throw std::invalid_argument("usage: iteration_benchmark [count] [iterations] [reverse_order=0|1]");
		}
		std::cout << "type,count,operation,allocation_order,reference,sample,before_ns,after_ns,vector_ns,before_iterator_bytes,after_iterator_bytes,checksum\n";
		run<std::uint64_t>(count, iterations, reverse, "u64");
		run<record>(count, iterations, reverse, "record64");
	} catch (const std::exception& error) {
		std::cerr << "Iteration benchmark failure: " << error.what() << '\n';
		return 1;
	}
}
