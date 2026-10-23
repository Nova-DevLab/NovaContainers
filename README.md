# NovaContainers

NovaContainers는 대량 데이터 처리에서 낮은 메모리 사용량과 빠른 조회를 목표로 하는 C++20 헤더 전용 컨테이너 라이브러리입니다. open addressing 기반 `nova::hash_map`, `nova::hash_set`과 정렬된 연속 저장 기반 `nova::flat_map`, `nova::flat_set`을 제공합니다.

## 주요 특징

- C++20 헤더 전용 라이브러리
- SSE2 16바이트 group 단위 control-byte 탐색
- 빈 map의 첫 삽입 또는 `reserve()`까지 heap 할당 지연
- `try_emplace`, `insert_or_assign`, iterator 기반 `erase` 지원
- value 오버헤드 없이 key만 직접 저장하는 `hash_set`
- transparent hash/equality를 이용한 heterogeneous lookup 지원
- stateful hash, equality, allocator의 복사·이동·전파 경로 지원
- 고정 최대 load factor `0.875`
- 정렬 순회와 이진 탐색을 제공하는 연속 저장 `flat_map`, `flat_set`
- `assign_unsorted`, `assign_sorted_unique`를 이용한 대량 데이터 일괄 구성
- transparent comparator를 이용한 flat 컨테이너 heterogeneous lookup
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

	nova::hash_set<std::string> active_users{"alice", "carol"};
	active_users.insert("dave");
	if (active_users.contains("carol")) {
		std::cout << "carol is active\n";
	}

	nova::flat_map<std::string, int> sorted_scores{
		{"carol", 30}, {"alice", 10}, {"bob", 20}};
	for (auto&& [name, score] : sorted_scores) {
		std::cout << name << ": " << score << '\n';
	}
}
```

개별 헤더인 `<NovaContainers/hash_map.hpp>`, `<NovaContainers/hash_set.hpp>`, `<NovaContainers/flat_map.hpp>`, `<NovaContainers/flat_set.hpp>`를 직접 포함해도 됩니다. 모든 타입은 소문자 이름과 PascalCase 별칭을 함께 제공하며, 새 코드에는 STL과 형태가 가까운 소문자 이름을 권장합니다.

## 컨테이너 선택

| 조건 | 권장 컨테이너 |
|---|---|
| 삽입·삭제가 빈번하고 평균 O(1) 조회가 중요 | `hash_map`, `hash_set` |
| 초기 구성 후 조회·정렬 순회가 대부분 | `flat_map`, `flat_set` |
| 메모리 밀도와 순차 접근 성능이 최우선 | `flat_map`, `flat_set` |

flat 컨테이너는 정렬된 `vector` 형태로 원소를 직접 저장합니다. 조회는 O(log N), 중간 삽입과 삭제는 O(N)입니다. 대량 입력은 개별 삽입 대신 `assign_unsorted`로 한 번에 정렬하거나, 이미 정렬된 고유 입력에는 `assign_sorted_unique`를 사용하는 것이 좋습니다.

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

최상위 프로젝트로 구성하면 테스트와 benchmark가 기본 활성화됩니다. `add_subdirectory()`로 포함하면 두 옵션의 기본값은 `OFF`이며, 명시적으로 설정한 값은 유지됩니다. C++20과 AVX2 요구 사항은 라이브러리 target을 통해 전파하고, 경고 옵션은 자체 테스트와 benchmark에만 적용합니다.

## 소스 구조

```text
include/NovaContainers/
  NovaContainers.hpp       # 집계 헤더
  hash_map.hpp, hash_set.hpp
  flat_map.hpp, flat_set.hpp
  detail/
    concepts.hpp           # 공통 타입 및 이종 키 조회 제약
    key_extractors.hpp     # map/set 키 추출
    hash/
      raw_hash_table.hpp   # 해시 저장소, 탐색 및 수명 관리
      simd_group.hpp       # control-byte SIMD 탐색
    flat/
      flat_table.hpp       # 정렬 저장소와 일괄 구성
      map_iterator.hpp     # key 변경을 막는 map proxy iterator
cmake/                     # 개발 실행 파일 공통 설정
tests/                    # hash/ 및 flat/ 기능 테스트
benchmarks/                # 독립 성능 측정 실행 파일
```

공개 include 경로와 소문자/PascalCase API는 유지합니다. `detail/` 아래 파일은 내부 구현이며 직접 포함하는 외부 코드의 호환 대상이 아닙니다. 내부 설계와 변경 시 지켜야 할 조건은 [구조 문서](docs/architecture.md)에 정리되어 있습니다.

## 제공 API

| 컨테이너 | 영역 | 주요 API |
|---|---|---|
| `hash_map` | 삽입·갱신 | `insert`, `emplace`, `try_emplace`, `insert_or_assign`, `operator[]` |
| `hash_set` | 삽입 | `insert`, `emplace` |
| `flat_map` | 삽입·갱신 | `insert`, `emplace`, `try_emplace`, `insert_or_assign`, `operator[]` |
| `flat_set` | 삽입 | `insert`, `emplace` |
| 공통 | 조회 | `find`, `contains`, `count` |
| flat 컨테이너 | 정렬 조회 | `lower_bound`, `upper_bound`, `equal_range` |
| 공통 | 삭제 | `erase(key)`, `erase(iterator)`, `clear` |
| hash 컨테이너 | 용량 | `size`, `capacity`, `load_factor`, `reserve`, `rehash` |
| flat 컨테이너 | 용량·일괄 구성 | `size`, `capacity`, `reserve`, `shrink_to_fit`, `assign_unsorted`, `assign_sorted_unique` |
| 공통 | 반복 | `begin`, `end`, `cbegin`, `cend` |
| 공통 | 상태·교환 | `get_allocator`, `Swap` |

`hash_map`과 `flat_map`은 누락 key에서 예외를 던지는 `at`도 제공합니다.

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

Release benchmark는 map과 set을 각각 표준 컨테이너와 비교합니다.

```powershell
.\build\benchmarks\Release\nova_containers_benchmark.exe
```

실제 서비스형 데이터는 UTF-8 `key<TAB>int64_value` 형식으로 입력할 수 있습니다.

```powershell
.\build\benchmarks\Release\nova_containers_benchmark.exe `
  --workload service `
  --dataset D:\data\anonymous_tags.tsv `
  --iterations 20
```

`--implementation std|nova|both`, `--workload int|service|all`, `--synthetic-count`, `--iterations` 옵션을 지원합니다. RSS를 비교할 때는 각 구현을 별도 프로세스로 실행해야 합니다.

## Iterator 무효화

### Hash 컨테이너

| 연산 | 영향 |
|---|---|
| 조회 | 무효화 없음 |
| 재해시 없는 삽입 | 기존 원소의 iterator와 reference 유지 |
| 삭제 | 삭제된 원소만 무효화 |
| `clear` | 모두 무효화 |
| `reserve`, `rehash`, 성장 삽입 | 모두 무효화 |

순회 순서는 hash, capacity, 삽입과 삭제 이력에 따라 달라지며 안정적이지 않습니다.

### Flat 컨테이너

| 연산 | 영향 |
|---|---|
| 조회 | 무효화 없음 |
| 끝 삽입, `reserve` 범위 안 | 기존 iterator와 reference 유지 |
| 중간 삽입·삭제 | 연산 위치 이후 모두 무효화 |
| 재할당, `assign_*`, `clear` | 모두 무효화 |

flat 컨테이너의 순회는 comparator 기준 key 정렬 순서입니다. `flat_map` iterator는 key를 `const Key&`, mapped value를 `Value&`로 노출하므로 구조적 바인딩에는 `auto&& [key, value]` 또는 `const auto& [key, value]`를 사용합니다.

## 현재 제약사항

- `max_load_factor()`는 `0.875` 조회 전용이며 설정 API는 제공하지 않습니다.
- allocator는 rebind 후 raw slot pointer를 반환해야 합니다. fancy pointer allocator는 지원하지 않습니다.
- control-byte 배열은 사용자 allocator가 아닌 별도 배열로 관리합니다.
- move-only value의 재해시 중 이동이 예외를 던지면 basic exception guarantee를 제공합니다.
- flat 컨테이너의 중간 삽입·삭제는 원소 이동이 필요하므로 저장 타입이 이동 가능한 형태여야 합니다.
- `assign_unsorted`는 중복 key를, `assign_sorted_unique`는 중복 또는 정렬되지 않은 입력을 `std::invalid_argument`로 거부합니다.
- 성능과 RSS 수치는 컴파일러, CPU, key 분포에 따라 달라지므로 실제 데이터로 다시 측정해야 합니다.

## 라이선스

[MIT License](LICENSE)
