#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#endif

#include "NovaContainers/NovaContainers.hpp"

struct BenchmarkResult {
	int64_t insert_ms = 0;
	int64_t find_ms = 0;
	int64_t missing_find_ms = 0;
	int64_t erase_ms = 0;
	int64_t sum = 0;
	std::size_t resident_delta_bytes = 0;
	std::size_t process_peak_bytes = 0;
};

struct ServiceRecord {
	std::string key;
	int64_t value = 0;
};

struct Options {
	std::string dataset_path;
	std::string implementation = "both";
	std::string workload = "all";
	std::size_t iterations = 5;
	std::size_t synthetic_count = 200000;
};

struct ProcessMemory {
	std::size_t resident_bytes = 0;
	std::size_t peak_bytes = 0;
};

ProcessMemory ReadProcessMemory() {
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX counters{};
	const BOOL succeeded = GetProcessMemoryInfo(
		GetCurrentProcess(),
		reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
		static_cast<DWORD>(sizeof(counters)));
	if (succeeded == FALSE) {
		return {};
	}
	return {counters.WorkingSetSize, counters.PeakWorkingSetSize};
#elif defined(__linux__)
	std::ifstream input("/proc/self/status");
	ProcessMemory memory;
	std::string line;
	while (std::getline(input, line)) {
		const bool is_resident = line.starts_with("VmRSS:");
		const bool is_peak = line.starts_with("VmHWM:");
		if (!is_resident && !is_peak) {
			continue;
		}
		std::istringstream value_stream(line.substr(6));
		std::size_t value_kib = 0;
		std::string unit;
		if (value_stream >> value_kib >> unit) {
			if (is_resident) {
				memory.resident_bytes = value_kib * 1024;
			} else {
				memory.peak_bytes = value_kib * 1024;
			}
		}
	}
	return memory;
#else
	return {};
#endif
}

std::size_t ParseSize(std::string_view text, const char* option_name) {
	std::size_t result = 0;
	const char* const begin = text.data();
	const char* const end = begin + text.size();
	const std::from_chars_result parse_result = std::from_chars(begin, end, result);
	if (parse_result.ec != std::errc{} || parse_result.ptr != end || result == 0) {
		throw std::invalid_argument(std::string(option_name) + " must be a positive integer");
	}
	return result;
}

Options ParseOptions(int argc, char* argv[]) {
	Options options;
	for (int index = 1; index < argc; ++index) {
		const std::string_view argument(argv[index]);
		if (argument == "--help") {
			std::cout
				<< "Usage: nova_containers_benchmark [options]\n"
				<< "  --dataset <path>          UTF-8 key<TAB>int64 dataset\n"
				<< "  --implementation <name>  both, std, or nova\n"
				<< "  --workload <name>        all, int, or service\n"
				<< "  --iterations <count>     Lookup repetitions (default: 5)\n"
				<< "  --synthetic-count <n>    Generated record count (default: 200000)\n";
			std::exit(0);
		}
		if (index + 1 >= argc) {
			throw std::invalid_argument(std::string(argument) + " requires a value");
		}
		const std::string_view value(argv[++index]);
		if (argument == "--dataset") {
			options.dataset_path = value;
		} else if (argument == "--implementation") {
			options.implementation = value;
		} else if (argument == "--workload") {
			options.workload = value;
		} else if (argument == "--iterations") {
			options.iterations = ParseSize(value, "--iterations");
		} else if (argument == "--synthetic-count") {
			options.synthetic_count = ParseSize(value, "--synthetic-count");
		} else {
			throw std::invalid_argument("unknown option: " + std::string(argument));
		}
	}
	if (options.implementation != "both" && options.implementation != "std" &&
		options.implementation != "nova") {
		throw std::invalid_argument("--implementation must be both, std, or nova");
	}
	if (options.workload != "all" && options.workload != "int" &&
		options.workload != "service") {
		throw std::invalid_argument("--workload must be all, int, or service");
	}
	return options;
}

std::vector<ServiceRecord> LoadDataset(const std::string& path) {
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		throw std::runtime_error("failed to open dataset: " + path);
	}

	std::vector<ServiceRecord> records;
	std::string line;
	std::size_t line_number = 0;
	while (std::getline(input, line)) {
		++line_number;
		if (line_number == 1 && line.size() >= 3 &&
			static_cast<unsigned char>(line[0]) == 0xEF &&
			static_cast<unsigned char>(line[1]) == 0xBB &&
			static_cast<unsigned char>(line[2]) == 0xBF) {
			line.erase(0, 3);
		}
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}

		const std::size_t separator = line.find('\t');
		ServiceRecord record;
		if (separator == std::string::npos) {
			record.key = std::move(line);
			record.value = static_cast<int64_t>(line_number);
		} else {
			record.key = line.substr(0, separator);
			const std::string_view value_text(line.data() + separator + 1,
				line.size() - separator - 1);
			const char* const begin = value_text.data();
			const char* const end = begin + value_text.size();
			const std::from_chars_result result =
				std::from_chars(begin, end, record.value);
			if (record.key.empty() || result.ec != std::errc{} || result.ptr != end) {
				throw std::runtime_error("invalid dataset row: " +
					std::to_string(line_number));
			}
		}
		records.push_back(std::move(record));
	}
	if (records.empty()) {
		throw std::runtime_error("dataset contains no records: " + path);
	}
	return records;
}

std::vector<ServiceRecord> GenerateSyntheticDataset(std::size_t count) {
	constexpr std::string_view kSignals[] = {
		"STATUS", "VALUE", "QUALITY", "ALARM.HIGH", "ALARM.LOW", "TIMESTAMP"};
	std::vector<ServiceRecord> records;
	records.reserve(count);
	for (std::size_t index = 0; index < count; ++index) {
		std::string key = "PLANT." + std::to_string(index % 8) + ".AREA." +
			std::to_string(index % 128) + ".DEVICE." + std::to_string(index / 6) +
			"." + std::string(kSignals[index % std::size(kSignals)]);
		records.push_back({std::move(key), static_cast<int64_t>(index)});
	}
	return records;
}

template <typename Map, typename Key>
BenchmarkResult RunReservedBenchmark(const std::vector<Key>& keys) {
	Map map;
	if constexpr (requires(Map& candidate) { candidate.max_load_factor(0.875F); }) {
		map.max_load_factor(0.875F);
	}
	map.reserve(keys.size());

	const ProcessMemory baseline = ReadProcessMemory();
	const auto start = std::chrono::steady_clock::now();
	for (std::size_t index = 0; index < keys.size(); ++index) {
		map.insert_or_assign(keys[index], static_cast<int64_t>(index));
	}
	const auto insert_end = std::chrono::steady_clock::now();
	const ProcessMemory after_insert = ReadProcessMemory();

	int64_t sum = 0;
	for (const Key& key : keys) {
		const auto iterator = map.find(key);
		if (iterator != map.end()) {
			sum += iterator->second;
		}
	}
	const auto find_end = std::chrono::steady_clock::now();

	return {
		std::chrono::duration_cast<std::chrono::milliseconds>(insert_end - start).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(find_end - insert_end).count(),
		0,
		0,
		sum,
		after_insert.resident_bytes > baseline.resident_bytes
			? after_insert.resident_bytes - baseline.resident_bytes
			: 0,
		after_insert.peak_bytes,
	};
}

template <typename Set, typename Key>
BenchmarkResult RunReservedSetBenchmark(const std::vector<Key>& keys) {
	Set set;
	if constexpr (requires(Set& candidate) { candidate.max_load_factor(0.875F); }) {
		set.max_load_factor(0.875F);
	}
	set.reserve(keys.size());

	const ProcessMemory baseline = ReadProcessMemory();
	const auto start = std::chrono::steady_clock::now();
	for (const Key& key : keys) {
		set.insert(key);
	}
	const auto insert_end = std::chrono::steady_clock::now();
	const ProcessMemory after_insert = ReadProcessMemory();

	int64_t sum = 0;
	for (const Key& key : keys) {
		if (set.find(key) != set.end()) {
			++sum;
		}
	}
	const auto find_end = std::chrono::steady_clock::now();

	return {
		std::chrono::duration_cast<std::chrono::milliseconds>(insert_end - start).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(find_end - insert_end).count(),
		0,
		0,
		sum,
		after_insert.resident_bytes > baseline.resident_bytes
			? after_insert.resident_bytes - baseline.resident_bytes
			: 0,
		after_insert.peak_bytes,
	};
}

template <typename Map>
BenchmarkResult RunServiceBenchmark(const std::vector<ServiceRecord>& records,
	const std::vector<std::string>& missing_keys, std::size_t iterations) {
	Map map;
	if constexpr (requires(Map& candidate) { candidate.max_load_factor(0.875F); }) {
		map.max_load_factor(0.875F);
	}
	map.reserve(records.size());

	const ProcessMemory baseline = ReadProcessMemory();
	const auto start = std::chrono::steady_clock::now();
	for (const ServiceRecord& record : records) {
		map.insert_or_assign(record.key, record.value);
	}
	const auto insert_end = std::chrono::steady_clock::now();
	const ProcessMemory after_insert = ReadProcessMemory();

	int64_t sum = 0;
	for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
		for (const ServiceRecord& record : records) {
			const auto iterator = map.find(record.key);
			if (iterator != map.end()) {
				sum += iterator->second;
			}
		}
	}
	const auto find_end = std::chrono::steady_clock::now();

	for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
		for (const std::string& key : missing_keys) {
			if (map.find(key) != map.end()) {
				throw std::runtime_error("unexpected missing-key match");
			}
		}
	}
	const auto missing_find_end = std::chrono::steady_clock::now();

	for (std::size_t index = 0; index < records.size(); index += 4) {
		map.erase(records[index].key);
	}
	const auto erase_end = std::chrono::steady_clock::now();

	return {
		std::chrono::duration_cast<std::chrono::milliseconds>(insert_end - start).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(find_end - insert_end).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(
			missing_find_end - find_end).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(
			erase_end - missing_find_end).count(),
		sum,
		after_insert.resident_bytes > baseline.resident_bytes
			? after_insert.resident_bytes - baseline.resident_bytes
			: 0,
		after_insert.peak_bytes,
	};
}

template <typename Set>
BenchmarkResult RunServiceSetBenchmark(const std::vector<ServiceRecord>& records,
	const std::vector<std::string>& missing_keys, std::size_t iterations) {
	Set set;
	if constexpr (requires(Set& candidate) { candidate.max_load_factor(0.875F); }) {
		set.max_load_factor(0.875F);
	}
	set.reserve(records.size());

	const ProcessMemory baseline = ReadProcessMemory();
	const auto start = std::chrono::steady_clock::now();
	for (const ServiceRecord& record : records) {
		set.insert(record.key);
	}
	const auto insert_end = std::chrono::steady_clock::now();
	const ProcessMemory after_insert = ReadProcessMemory();

	int64_t sum = 0;
	for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
		for (const ServiceRecord& record : records) {
			if (set.find(record.key) != set.end()) {
				++sum;
			}
		}
	}
	const auto find_end = std::chrono::steady_clock::now();

	for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
		for (const std::string& key : missing_keys) {
			if (set.find(key) != set.end()) {
				throw std::runtime_error("unexpected missing-key match");
			}
		}
	}
	const auto missing_find_end = std::chrono::steady_clock::now();

	for (std::size_t index = 0; index < records.size(); index += 4) {
		set.erase(records[index].key);
	}
	const auto erase_end = std::chrono::steady_clock::now();

	return {
		std::chrono::duration_cast<std::chrono::milliseconds>(insert_end - start).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(find_end - insert_end).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(
			missing_find_end - find_end).count(),
		std::chrono::duration_cast<std::chrono::milliseconds>(
			erase_end - missing_find_end).count(),
		sum,
		after_insert.resident_bytes > baseline.resident_bytes
			? after_insert.resident_bytes - baseline.resident_bytes
			: 0,
		after_insert.peak_bytes,
	};
}

void PrintResult(const char* name, const BenchmarkResult& result, bool extended = false) {
	std::cout << ' ' << name << " -> Insert: " << result.insert_ms
		<< " ms | Find: " << result.find_ms << " ms";
	if (extended) {
		std::cout << " | Miss: " << result.missing_find_ms << " ms | Erase 25%: "
			<< result.erase_ms << " ms";
	}
	std::cout << " | RSS delta: " << result.resident_delta_bytes / 1024
		<< " KiB | Process peak: " << result.process_peak_bytes / 1024
		<< " KiB (sum: " << result.sum << ")\n";
}

int main(int argc, char* argv[]) {
	try {
		const Options options = ParseOptions(argc, argv);
		std::cout << "===========================================\n";
		std::cout << " [Benchmark] NovaContainers vs standard containers\n";
		std::cout << "===========================================\n";

		if (options.workload != "service") {
			constexpr int kCount = 1000000;
			std::vector<int> integer_keys;
			integer_keys.reserve(kCount);
			for (int index = 0; index < kCount; ++index) {
				integer_keys.push_back(index);
			}

			std::cout << "\n[Reserved 87.5% load target, int keys]\n";
			if (options.implementation != "nova") {
				PrintResult("std::unordered_map",
					RunReservedBenchmark<std::unordered_map<int, int64_t>>(integer_keys));
			}
			if (options.implementation != "std") {
				PrintResult("nova::hash_map    ",
					RunReservedBenchmark<nova::hash_map<int, int64_t>>(integer_keys));
			}

			std::cout << "\n[Reserved 87.5% load target, int set keys]\n";
			if (options.implementation != "nova") {
				PrintResult("std::unordered_set",
					RunReservedSetBenchmark<std::unordered_set<int>>(integer_keys));
			}
			if (options.implementation != "std") {
				PrintResult("nova::hash_set    ",
					RunReservedSetBenchmark<nova::hash_set<int>>(integer_keys));
			}
		}

		if (options.workload != "int") {
			std::vector<ServiceRecord> records = options.dataset_path.empty()
				? GenerateSyntheticDataset(options.synthetic_count)
				: LoadDataset(options.dataset_path);
			std::vector<std::string> missing_keys;
			missing_keys.reserve(records.size());
			for (const ServiceRecord& record : records) {
				missing_keys.push_back(record.key + ".MISSING");
			}

			std::cout << "\n[Service-shaped string workload: " << records.size()
				<< " records, " << options.iterations << " lookup iterations]\n";
			std::cout << " Dataset: "
				<< (options.dataset_path.empty()
					? "generated SCADA-like keys"
					: options.dataset_path)
				<< '\n';
			if (options.implementation != "nova") {
				PrintResult("std::unordered_map",
					RunServiceBenchmark<std::unordered_map<std::string, int64_t>>(
						records, missing_keys, options.iterations), true);
			}
			if (options.implementation != "std") {
				PrintResult("nova::hash_map    ",
					RunServiceBenchmark<nova::hash_map<std::string, int64_t>>(
						records, missing_keys, options.iterations), true);
			}

			std::cout << "\n[Service-shaped string set workload]\n";
			if (options.implementation != "nova") {
				PrintResult("std::unordered_set",
					RunServiceSetBenchmark<std::unordered_set<std::string>>(
						records, missing_keys, options.iterations), true);
			}
			if (options.implementation != "std") {
				PrintResult("nova::hash_set    ",
					RunServiceSetBenchmark<nova::hash_set<std::string>>(
						records, missing_keys, options.iterations), true);
			}
		}

		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "Benchmark error: " << exception.what() << '\n';
		return 1;
	}
}
