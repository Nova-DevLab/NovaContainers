# nova::list 설계와 검증

`nova::list<T, allocator_type, BlockBytes>`는 연결 리스트가 아닌 블록형 시퀀스입니다. 이번 작업은 속도와 알고리즘을 우선하고, 메모리 최적화는 후속 과제로 둡니다.

## 저장과 접근

- 원소는 블록 안에서 연속 저장하며, 양끝 확장 시 기존 원소를 이동하지 않습니다.
- 블록 포인터 디렉터리는 기하급수적으로 확장합니다. 디렉터리 재할당 시 블록 포인터만 이동합니다.
- 기본 BlockBytes는 16384입니다. 블록당 원소 수 `k_block_capacity`는 `max(1, BlockBytes / sizeof(T))` 이하의 가장 큰 2의 거듭제곱입니다. 최적값 보장이 아닌 조정점입니다.
- `operator[]`는 O(1)이며 블록 번호와 내부 위치를 계산합니다.
- 일반 iterator는 블록 내부에서 포인터를 증가시키고 경계에서만 다음 블록을 찾습니다. `blocks()`는 실제 원소 영역을 `std::span<T>` 또는 `std::span<const T>`로 제공합니다.
- 전체 연속성을 보장하지 않으므로 단일 `data()`는 제공하지 않습니다.

## API

공개 API와 내부 구현은 `snake_case`로 통일하며 `nova::list`는 `nova::containers::list`의 별칭입니다.

```cpp
nova::list<int> values;
values.reserve_back(100000);
for (int i = 0; i < 100000; ++i) {
	values.emplace_back(i);
}
values.emplace_front(-1);
values.insert(values.begin() + 10, 42);
std::sort(values.begin(), values.end());
for (auto block : values.blocks()) {
	for (int& value : block) {
		value += 1;
	}
}
```

- `push_front/back`, `emplace_front/back`, `pop_front/back`, `front/back`, `operator[]`, `at`
- random-access iterator, const iterator, reverse iterator, 표준 정렬 알고리즘 연동
- 위치 기반 `insert/emplace/erase`, count/range 삽입과 범위 삭제, `assign`, `resize`
- `reserve_front(n)`, `reserve_back(n)`은 해당 방향에 n개를 더 추가할 빈 슬롯을 확보합니다. `reserve(n)`은 전체 크기가 n개가 되도록 뒤쪽 슬롯을 확보합니다.
- `capacity()`는 디렉터리에서 추적하는 사용 가능한 슬롯 수이며 메인 배열의 실제 보유량과 다를 수 있습니다. `front_capacity()`, `back_capacity()`는 방향별 빈 슬롯 수입니다.
- 중간 삽입·삭제는 가까운 끝 방향으로 원소를 이동합니다. 범위 삽입은 입력을 임시 저장한 뒤 회전하므로 자기 범위 삽입도 지원합니다.

## 수명과 무효화

- 양끝 추가는 기존 원소 주소를 유지합니다. 디렉터리 재할당은 iterator를 무효화할 수 있으므로 삽입 및 reserve 이후 iterator를 다시 얻는 것이 안전합니다. 기존 원소의 span은 유지됩니다.
- 양끝 삭제 시 삭제한 원소의 참조·span은 무효입니다. 해당 방향에서 사용하지 않게 된 블록은 반환할 수 있습니다.
- 중간 삽입·삭제, 대입, clear 이후 기존 iterator·참조·span을 재사용하지 않습니다.
- 메모리 정리와 연속 재구성은 `optimize()` 하나로 수행합니다. 과도한 메인 배열과 예약 블록을 반환하며, 실제 재구성 시 원소 주소·iterator·span이 무효화됩니다. 최소 저장 상태이면 재구성을 생략합니다. `shrink_to_fit()`은 제공하지 않습니다.
- swap 시 원소와 블록은 상대 컨테이너로 이동합니다. iterator와 blocks 범위는 다시 얻는 것을 계약으로 합니다.

## allocator와 예외

- 사용자 allocator는 원소 블록의 할당·생성·파괴에 사용합니다. 디렉터리는 표준 allocator로 관리하며 fancy pointer는 지원하지 않습니다.
- 복사·이동·교환 시 allocator 전파 특성을 따릅니다. allocator가 다른 비전파 swap은 std::invalid_argument로 거부합니다.
- reserve 또는 원소 생성 실패 시 기존 원소 수를 유지하지만 여유 용량은 바뀔 수 있습니다.
- 중간 이동·회전 또는 allocator가 다른 이동 중 예외는 basic guarantee를 제공합니다. 원소 값은 바뀔 수 있고 범위 삽입 회전 실패 시 추가한 원소가 남을 수 있습니다.
- rvalue 인자로 넘긴 원소 자체는 생성 실패 시에도 이동되어 있을 수 있습니다.

## 검증과 성능

`nova_containers_list_tests`는 경계, iterator, 차등 테스트, 주소 안정성, allocator, 예외, 수명을 검증합니다.

`nova_containers_list_benchmark [count] [iterations]`는 vector, deque, nova::list의 일반 순회, 인덱스 순회, 랜덤 조회, 뒤 추가, 양끝 추가, 중간 변경, 정렬을 비교합니다. nova::list는 블록별 순회도 측정합니다.

생성·복사·파괴가 포함된 항목은 출력 이름에 표시합니다. 정렬 항목은 정렬 결과 확인 비용도 포함합니다. 기본 벤치는 5회 측정 중앙값을 CSV로 출력합니다.

측정 결과는 아래와 같습니다. 이 장비와 워크로드에 한정된 결과이며 다른 환경에서의 성능 우위를 보장하지 않습니다.


### 2026-11-04 검증 결과

- CPU: Intel Core i7-1165G7, 4코어/8논리 프로세서.
- MSVC 파일 버전 19.51.36260.0, Visual Studio 18 2026 / MSBuild 18.11.0, CMake 4.3.1-msvc1.
- x64, C++20, Release, AVX2. 기본 블록 목표 16KiB.
- 빌드 및 실행 부모 프로세스 BelowNormal, affinity=1을 확인하고 자식에 상속했습니다. 빌드 `--parallel 1 -- /maxcpucount:1 /p:CL_MPCount=1`, `_CL_=/cgthreads1`, `_LINK_=/CGTHREADS:1`을 적용했습니다.
- 워밍업 후 같은 데이터를 반복 순회하며 5개 샘플의 중앙값을 사용했습니다. 10만 건은 샘플당 30회, 100만 건은 15회입니다. 실행 순서는 vector, deque, nova이며 실행 순서 무작위화 및 하드웨어 카운터 분석은 하지 않았습니다.
- 기존 hash map/set, flat map/set, 신규 list를 포함한 전체 Release 테스트 4/4 통과. 신규 list는 Release AddressSanitizer 1/1 통과.
- UTF-8 BOM 및 CP949로 표현 가능한 문자 검증을 통과했습니다.

아래는 단순 합산 1회당 시간이며 단위는 마이크로초입니다.

| 원소 타입 / 건수 | vector 일반 순회 | deque 일반 순회 | list 일반 순회 | list 블록 순회 |
|---|---:|---:|---:|---:|
| u64 / 10만 | 5.62 | 70.67 | 39.46 | 6.47 |
| u64 / 100만 | 154.81 | 1731.55 | 504.22 | 182.24 |
| record64 / 10만 | 110.72 | 173.43 | 116.87 | 121.06 |
| record64 / 100만 | 3465.11 | 4886.82 | 3855.97 | 3560.10 |

**미해결 성능 항목:** uint64_t 일반 순회는 vector 대비 3.26~7.02배 시간이 걸립니다. 같은 조건의 deque보다는 1.79~3.43배 빠르지만, 일반 순회를 벡터 수준으로 만드는 목표는 아직 달성하지 못했습니다. 블록별 순회는 vector 대비 1.15~1.18배 시간입니다. 64바이트 구조체 일반 순회는 vector 대비 1.06~1.11배 시간입니다.

작은 원소 일반 순회의 차이는 블록 경계가 있는 루프와 컴파일러 자동 벡터화 차이의 영향으로 추정합니다. 어셈블리 및 성능 카운터 분석으로 원인을 확정하지 않았습니다. 기본 블록 크기와 다른 블록 크기의 A/B 측정도 아직 하지 않았습니다.

랜덤 조회, 인덱스 순회, 뒤 추가, 양끝 추가, 중간 변경, 정렬의 전체 원본: [10만 건 CSV](benchmarks/list_2026-11-04_100k.csv), [100만 건 CSV](benchmarks/list_2026-11-04_1m.csv). 뒤 추가는 생성·파괴를, 중간 변경은 복사·100쌍 삽입삭제·파괴를, 정렬은 복사·정렬·정렬 확인·파괴를 포함합니다. 양끝 추가는 vector 기준 실행 비용을 고려해 1만 건으로 제한했습니다.

후속 속도 검토 후보는 블록 크기 A/B, iterator 경계 처리와 생성 코드 분석, 블록 단위 알고리즘입니다. 메모리 최적화는 속도 목표 검토 후 별도로 진행합니다.

## 후속 순회 성능 검토

일반 iterator 개선안과 별도 `for_each` 속도 경로의 검토는 [순회 성능 검토](list_iteration_optimization.md)에 기록합니다. 최초 구현의 위 측정과 후속 측정은 실행 조건이 다르므로 원시 시간을 직접 비교하지 않습니다.
