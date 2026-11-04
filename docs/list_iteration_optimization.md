# 순회 성능 검토

이 문서의 벤치마크 원자료, 소스 해시와 패치는 snake_case 일괄 변경 이전의 구현을 기록합니다. 원자료는 보존하며, 과거 패치를 현재 헤더에 직접 역적용하지 않습니다. 재현 시 기록된 해시에 해당하는 이전 소스가 필요합니다.

이번 검토는 기존 `for (const auto& value : items)`의 성능 개선부터 시도했습니다. iterator 변경 후보에서는 일부 조건의 이득과 다른 조건의 회귀가 함께 나와 채택하지 않았습니다. 기존 16바이트 iterator를 유지하고, 블록별 연속 영역을 내부에서 처리하는 `for_each`를 별도 속도 경로로 추가합니다.

## 사용법과 계약

```cpp
std::uint64_t sum = 0;
items.for_each([&](const auto& value) {
	sum += value;
});
```

방문 순서는 일반 순회와 동일합니다. 사용자는 블록별 중첩 루프를 작성하지 않으며, 구현 내부에서는 각 블록의 연속 영역에 callable을 적용합니다. `std::invoke`를 사용하며 const 컨테이너에는 const 원소 참조를 전달합니다. 상태가 있는 callable을 반환하므로 최종 상태를 받을 수 있습니다. move-only callable도 지원합니다. 원래 callable 객체에 상태를 누적하려면 `std::ref`를 전달합니다. 임시 컨테이너에서의 호출은 허용하지 않습니다. callable이 예외를 던지면 즉시 중단하고 예외를 전파합니다.

콜백에서 컨테이너의 크기나 구조를 바꾸지 않습니다. 원소 값 변경은 비const overload에서 가능합니다. 이 API의 측정 결과를 기존 range-for 자체의 개선으로 해석하면 안 됩니다.

## 검토했으나 채택하지 않은 후보

- 원소 포인터만으로 종료 비교: 일관된 단독 이득을 확인하지 못했습니다.
- 블록 시작·끝 포인터를 캐시한 24바이트 iterator: 정방향의 작은 이득이 일부 있었지만 역방향 또는 조건 순회에서 회귀가 나타났습니다.
- 끝을 nullptr로 정규화하고 전체 끝 위치를 캐시한 40바이트 iterator: 생성 함수의 비인라인화 및 임시 객체 비용으로 역방향 회귀가 컸습니다. 인라인 조정 후에도 채택할 근거가 부족했습니다.
- 블록 내부 순번을 저장하는 16바이트 iterator: 작은 원소의 정방향 순회가 더 느렸습니다.
- 직접 구현한 reverse adaptor: 임시 복사는 줄였지만 일관된 전체 성능 이득을 확인하지 못했습니다.

MSVC 생성 코드에서 vector의 uint64_t 합산에는 AVX2 `vpaddq`가 사용되고, 기존 일반 list 순회는 scalar 합산임을 확인했습니다. 단순히 iterator 필드를 추가하는 것으로 이 차이를 해결하지는 못했습니다.

## 측정 방법

`nova_containers_list_iteration_benchmark [count] [iterations] [reverse_order=0|1]`는 일반 정방향 합산, `for_each` 합산, 역방향 합산, 조건 순회를 비교합니다. 기본 기준 컨테이너는 deque이며 vector 시간도 함께 출력합니다.

이번 A/B에서는 이전 list 헤더를 다른 네임스페이스 `nova_baseline`으로 격리해 동일 프로세스에서 이전 구현과 최종 구현을 비교했습니다. 이전 헤더 경로는 `NOVA_LIST_BASELINE_HEADER` compile definition으로 전달했습니다. 변경 전후를 번갈아 11회 측정하고 원본 샘플과 중앙값을 기록합니다. reverse_order를 바꾸면 원소 저장소의 할당 순서와 첫 측정 순서도 바뀝니다.

최종 측정값, 소스 해시, 원본 CSV 및 검증 결과는 아래와 같습니다.


## 최종 A/B 결과: 2026-11-04

- Intel Core i7-1165G7, MSVC 19.51.36260.0, x64 Release C++20, AVX2.
- 빌드와 측정은 BelowNormal, 논리 CPU 1 하나(affinity mask=2)에 제한했습니다. 빌드는 `--parallel 1 -- /maxcpucount:1 /p:CL_MPCount=1`, `_CL_=/cgthreads1`, `_LINK_=/CGTHREADS:1`을 적용했습니다.
- 10만 건은 샘플당 100회, 100만 건은 20회이며, 원본 11개 샘플과 중앙값을 보존했습니다. 다른 로컬 작업이 실행 중인 환경의 wall-clock 측정입니다.
- 최종 일반 iterator는 이전 구현과 동일한 16바이트입니다. 새 API의 비교 기준은 이전 list의 일반 range-for이며, 전체 구현의 성능 향상으로 해석하지 않습니다.

아래는 순차 합산 1회당 마이크로초입니다. 초기 할당 순서를 두 방향으로 바꾸었으며 모든 결과를 함께 제시합니다.

| 타입 / 건수 | 할당 순서 | 이전 일반 for | 새 for_each | vector 일반 for |
|---|---|---:|---:|---:|
| u64 / 10만 | 이전 먼저 | 63.12 | 55.44 | 10.10 |
| record64 / 10만 | 이전 먼저 | 284.63 | 280.40 | 285.73 |
| u64 / 10만 | 새 구현 먼저 | 67.92 | 55.29 | 9.93 |
| record64 / 10만 | 새 구현 먼저 | 323.53 | 325.85 | 329.35 |
| u64 / 100만 | 이전 먼저 | 821.86 | 720.95 | 299.56 |
| record64 / 100만 | 이전 먼저 | 4113.52 | 3754.74 | 3724.59 |
| u64 / 100만 | 새 구현 먼저 | 1366.87 | 786.61 | 296.06 |
| record64 / 100만 | 새 구현 먼저 | 4061.30 | 3725.60 | 3741.40 |

10만 uint64_t 합산의 소요 시간은 두 할당 순서에서 약 12.2%, 18.6% 감소했습니다. 64바이트 구조체 10만 건은 실질적인 개선을 확정하기 어렵습니다. 100만 uint64_t의 두 순서에서는 이전 구현의 시간 차이가 커서 고정 개선율을 주장하지 않습니다.

원본 CSV의 `forward_sum`은 이전과 최종 구현이 동일한 일반 iterator를 사용하는 대조군입니다. 이 행의 시간 차이는 코드 개선으로 계산하지 않습니다. 역방향과 조건 순회도 대조군이며 미채택 iterator 변경의 효과를 최종 결과로 포함하지 않습니다.

최종 for_each 합산의 생성 코드도 scalar add였고 AVX2 vpaddq는 적용되지 않았습니다. 콜백 전달을 직접 호출 및 인덱스 루프로 바꾼 추가 후보도 SIMD와 일관된 이득을 얻지 못해 std::invoke 구현을 유지했습니다. 따라서 직접 작성한 블록 순회 및 vector 수준의 성능 목표는 아직 달성하지 못했습니다.

### 검증

- 최종 list Release CTest 통과.
- 최종 list Release AddressSanitizer CTest 통과.
- 기존 기능 외에 끝 경계·부분 범위 알고리즘 matrix, for_each 방문 순서·수정·const·move-only·상태 반환·reference_wrapper·멤버 포인터·예외 중단과 재사용을 검증했습니다.
- 독립 순회 benchmark의 기본 deque 비교 모드 빌드 및 checksum 스모크 통과.
- 최종 소스가 검증된 std::invoke 후보와 바이트 단위로 동일함을 확인했습니다.

### 원본과 재현

- [실행 환경과 소스 SHA-256](benchmarks/list_iteration_2026-11-04_environment.json)
- [최적화 전후 헤더 패치](benchmarks/list_iteration_2026-11-04.patch): 최종 헤더의 복사본에 역적용하면 측정 기준 헤더를 복원할 수 있습니다. 복원 후 SHA-256을 환경 기록과 비교합니다. 원본 작업 트리에 역적용하지 않습니다.
- [10만 건 / 이전 먼저 CSV](benchmarks/list_iteration_2026-11-04_100k_before_first.csv)
- [10만 건 / 새 구현 먼저 CSV](benchmarks/list_iteration_2026-11-04_100k_after_first.csv)
- [100만 건 / 이전 먼저 CSV](benchmarks/list_iteration_2026-11-04_1m_before_first.csv)
- [100만 건 / 새 구현 먼저 CSV](benchmarks/list_iteration_2026-11-04_1m_after_first.csv)

A/B 재현 시 복원한 헤더의 include guard를 고유하게 바꾸고 `namespace nova::containers`와 `namespace nova`를 각각 `nova_baseline::containers`와 `nova_baseline`으로 격리합니다. 이 파일을 `NOVA_LIST_BASELINE_HEADER` compile definition으로 지정해 같은 benchmark 소스를 빌드합니다. 일반 프로젝트 타깃은 이 definition 없이 deque 기준으로 동작합니다.

후속 과제는 일반 range-for의 블록 경계 처리와 컴파일러 벡터화 한계를 다루는 알고리즘·구조 검토입니다. SIMD를 강제하기 위해 임의 callback에 독립성 또는 비별칭 가정을 추가하지 않았습니다.


## 추가 진단: 고정 모드와 논리 위치 종료 비교

2026-11-04, 단일 uint64_t 10만 건 합산 케이스에서 기존 일반 순회의 MSVC 벡터화 실패를 조사했습니다. 라이브러리 원본과 기존 테스트는 변경하지 않았습니다.

- 원본 헤더 SHA-256: `a994ac3d7eb7c8bb853c9cb9d60d4d44eacca60a67cbaf642a73e9ecf7217cf3`. 현재 원본 iterator는 x64에서 24바이트입니다. 위의 16바이트 기록은 Optimize 추가 전 시점의 기록입니다.
- 이전 임시 후보: 증가 함수의 연속 경로 조기 반환, 모드 플래그를 논리 위치로 대체한 종료 비교 모두 MSVC 진단 507이 유지됐습니다.
- 이번 후보: 고정 모드 플래그를 유지하며 논리 위치를 추가하고, iterator의 동등 비교와 거리를 논리 위치로 계산했습니다. 연속 경로는 고정 시작 주소와 논리 인덱스로 역참조하고, 블록 경로는 기존 원소 포인터와 블록 전환을 사용했습니다.
- 생성 코드에서 두 모드의 루프가 분리됐지만 일반 range-for와 연속 여부를 먼저 검사한 range-for 모두 C5002, 이유 507이 유지됐습니다. 같은 값의 span 순회만 C5001과 AVX2 vpaddq를 확인했습니다. 원소 역참조의 불필요한 null 분기를 제거해도 결과는 같았습니다.
- 후보 iterator는 x64 32바이트로 원본보다 8바이트 증가합니다. 원소, 블록 디렉터리와 컨테이너의 저장 레이아웃은 변경하지 않았습니다. 성능 개선 근거 없이 iterator 크기를 늘리지 않도록 미채택했습니다.
- 검증: 임시 진단 프로그램을 Release 최적화로 빌드하고 분리/연속 상태의 10만 건 합산을 검증했습니다. 전체 CTest와 성능 시간 측정은 실행하지 않았으며, 전체 API 회귀 통과나 성능 향상을 주장하지 않습니다.
- 조건: MSVC 19.51, x64, /O2 /DNDEBUG /std:c++20 /EHsc /arch:AVX2 /Qvec-report:2, BelowNormal, 허용된 논리 CPU 하나, 빌드 작업 하나, /cgthreads1, /CGTHREADS:1.

507은 경계, 초기화, 종료 또는 비교 일관성 분석 실패를 묶은 진단입니다. 이 결과만으로 컴파일러 내부의 정확한 실패 단계를 확정하지 않습니다. 종료 조건 변경이나 루프 분리만으로 해결된다고 제안하지 않습니다. [MSVC 진단 코드 설명](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/vectorizer-and-parallelizer-messages)
