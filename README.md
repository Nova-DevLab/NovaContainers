# NovaContainers

NovaContainers는 대량 데이터 처리에서 낮은 메모리 사용량과 빠른 조회를 목표로 하는 C++20 헤더 전용 컨테이너 라이브러리입니다. 현재는 open addressing과 SIMD control-byte 탐색을 사용하는 `nova::hash_map`을 제공합니다.

## 주요 특징

- C++20 헤더 전용 라이브러리
- SSE2 16바이트 group 단위 control-byte 탐색
- 빈 map의 첫 삽입 또는 `reserve()`까지 heap 할당 지연
- `try_emplace`, `insert_or_assign`, iterator 기반 `erase` 지원
- transparent hash/equality를 이용한 heterogeneous lookup 지원
- stateful hash, equality, allocator의 복사·이동·전파 경로 지원
- 고정 최대 load factor `0.875`
- Debug/Release 차등 테스트와 AddressSanitizer 검증
- 실제 TSV 입력과 RSS 측정을 지원하는 독립 benchmark

## 요구 사항

- CMake 3.20 이상
- C++20을 지원하는 MSVC, GCC 또는 Clang
- AVX2를 지원하는 x86-64 CPU

프로젝트 CMake 설정은 MSVC에서 `/arch:AVX2`, GCC와 Clang에서 `-mavx2`를 사용합니다. 내부 control group 구현은 A/B 측정 결과에 따라 SSE2 16바이트 폭을 유지합니다.

## 빠른 시작

집계 헤더를 포함하고 STL과 비슷한 소문자 API를 사용할 수 있습니다.

```cpp
#include <iostream>
#include <string>

#include <NovaContainers/NovaContainers.hpp>

int main() {
	nova::hash_map<std::string, int> scores;
	scores.reserve(1024);

	scores.try_emplace("alice", 10);
	scores.insert_or_assign("bob", 20);
	scores["carol"] = 30;

	if (scores.contains("bob")) {
		std::cout << scores.at("bob") << '\n';
	}

	for (const auto& [name, score] : scores) {
		std::cout << name << ": " << score << '\n';
	}
}
```

개별 헤더인 `<NovaContainers/hash_map.hpp>`를 직접 포함해도 됩니다. 타입 이름은 `nova::hash_map<Key, Value>`와 `nova::HashMap<Key, Value>`를 모두 제공하며, 새 코드에는 STL과 형태가 가까운 `nova::hash_map`을 권장합니다.

## CMake 연동

소스 트리를 하위 디렉터리로 포함한 뒤 interface target을 연결합니다.

```cmake
add_subdirectory(path/to/NovaContainers)

target_link_libraries(my_application
  PRIVATE
    nova::containers
)
```

라이브러리만 사용할 때는 테스트와 benchmark를 끌 수 있습니다.

```powershell
cmake -S . -B build `
  -DNOVA_BUILD_TESTS=OFF `
  -DNOVA_BUILD_BENCHMARKS=OFF
```

## 제공 API

| 영역 | 주요 API |
|---|---|
| 삽입·갱신 | `insert`, `emplace`, `try_emplace`, `insert_or_assign`, `operator[]` |
| 조회 | `find`, `contains`, `count`, `at` |
| 삭제 | `erase(key)`, `erase(iterator)`, `clear` |
| 용량 | `size`, `empty`, `capacity`, `load_factor`, `max_load_factor`, `reserve`, `rehash` |
| 반복 | `begin`, `end`, `cbegin`, `cend` |
| 상태·교환 | `get_allocator`, `Swap` |

PascalCase API도 호환 목적으로 함께 제공됩니다.

## 빌드와 테스트

```powershell
cmake -S . -B build `
  -DNOVA_BUILD_TESTS=ON `
  -DNOVA_BUILD_BENCHMARKS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Benchmark

Release benchmark 기본 실행:

```powershell
.\build\tests\Release\nova_containers_benchmark.exe
```

실제 서비스형 데이터는 UTF-8 `key<TAB>int64_value` 형식으로 입력할 수 있습니다.

```powershell
.\build\tests\Release\nova_containers_benchmark.exe `
  --workload service `
  --dataset D:\data\anonymous_tags.tsv `
  --iterations 20
```

`--implementation std|nova|both`, `--workload int|service|all`, `--synthetic-count`, `--iterations` 옵션을 지원합니다. RSS를 비교할 때는 각 구현을 별도 프로세스로 실행해야 합니다.

## Iterator 무효화

| 연산 | 영향 |
|---|---|
| 조회 | 무효화 없음 |
| 재해시 없는 삽입 | 기존 원소의 iterator와 reference 유지 |
| 삭제 | 삭제된 원소만 무효화 |
| `clear` | 모두 무효화 |
| `reserve`, `rehash`, 성장 삽입 | 모두 무효화 |

순회 순서는 hash, capacity, 삽입과 삭제 이력에 따라 달라지며 안정적이지 않습니다.

## 현재 제약사항

- `max_load_factor()`는 `0.875` 조회 전용이며 설정 API는 제공하지 않습니다.
- allocator는 rebind 후 raw slot pointer를 반환해야 합니다. fancy pointer allocator는 지원하지 않습니다.
- control-byte 배열은 사용자 allocator가 아닌 별도 배열로 관리합니다.
- move-only value의 재해시 중 이동이 예외를 던지면 basic exception guarantee를 제공합니다.
- 성능과 RSS 수치는 컴파일러, CPU, key 분포에 따라 달라지므로 실제 데이터로 다시 측정해야 합니다.

## 라이선스

[MIT License](LICENSE)
