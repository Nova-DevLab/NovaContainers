# list 삽입 메모리 최적화

현재 구현: 길이를 아는 기본 list 반복자 및 같은 원소 타입의 연속 반복자는 대상에 직접 예약·생성합니다. 자기 범위는 인덱스로 보존하고 앞 삽입에 따른 위치 변화도 보정합니다. 다른 반복자는 작은 vector와 큰 list 버퍼 경로를 유지합니다. 최신 결과는 [직접 삽입 검증](list_direct_validation.md)을 참고합니다. 아래 수치와 변경 설명은 이전 vector 기반 구현의 역사적 기록입니다. 중간 단계의 [청크 실험](list_chunk_validation.md)과 최신 구현을 혼동하지 않습니다.

후속 API 정리에서 일반 `list::reserve()`를 제거하고 `reserve_front/back()`만 남겼습니다. 아래 측정과 소스 해시, 패치는 API 정리 전 스냅샷을 기록하며 이번 삭제 후 헤더의 해시와 구분합니다. 아래 최종 헤더 패치만 현재 헤더에 역적용해서는 당시의 전체 스냅샷을 복원할 수 없습니다.

2026-11-04 검토한 세 후보 중 두 삽입 최적화를 적용했습니다. 디렉터리 제자리 재배치는 성능 무저하를 확인하지 못하여 제외했습니다. 원소 블록 크기, 원소/iterator 레이아웃과 공개 함수 시그니처는 유지합니다.

## 당시 적용한 변경

- 개수 지정 insert는 count개짜리 임시 vector를 만들지 않고 가까운 끝에 value를 직접 복사 생성한 뒤 기존과 동일하게 rotate합니다. 양끝 확장은 원래 원소의 주소를 유지하므로 내부 원소를 value로 전달해도 복사 생성 중 참조가 유지됩니다. 복사 생성 실패 시 추가한 원소만 제거하며 원래 원소 수, 값, 주소를 유지합니다. 여유 용량은 기존 reserve/생성 실패 계약처럼 달라질 수 있습니다. rotate 실패 시 기존 basic guarantee를 유지합니다.
- 범위 insert는 std::sized_sentinel_for<InputIt, InputIt>일 때만 O(1) 거리 계산으로 임시 버퍼를 미리 reserve합니다. 단일 패스 입력과 일반 forward 범위는 추가 순회하지 않습니다. 자기 범위 삽입을 위한 임시 버퍼는 유지합니다. reserve는 미사용 T를 생성하지 않습니다.

## 독립 A/B 메모리 측정

원소 128개가 있는 list의 앞/중간/뒤에 100,000개를 삽입했습니다. 기본 BlockBytes=16384입니다. 아래는 뒤 삽입 작업 중의 최고 동시 할당 요청량이며 단위는 바이트입니다. 다른 삽입 위치의 원자료도 보존했습니다.

| 타입 | 개수 삽입 변경 전 | 개수 삽입 변경 후 | 범위 삽입 변경 전 | 범위 삽입 변경 후 |
|---|---:|---:|---:|---:|
| uint64_t (8B) | 1,605,582 | 805,543 | 2,711,661 | 2,405,621 |
| record64 (64B) | 12,844,198 | 6,444,159 | 21,692,557 | 19,244,237 |
| 짧은 string (객체 32B, 문자열 6자) | 6,422,115 | 3,222,076 | 10,846,314 | 9,622,154 |
| 긴 string (객체 32B, 문자열 96자) | 17,636,563 | 14,436,524 | 33,260,762 | 32,036,602 |

개수 삽입은 원본과 첫 후보를, 범위 삽입은 첫 후보와 두 번째 후보를 비교했습니다. 디렉터리, 원소 블록, 범위 입력 vector와 문자열 힙 저장소를 포함한 global new 요청 바이트입니다. MSVC STL이 정렬을 위해 추가 요청하는 바이트도 포함하므로 단순 count * sizeof(T)와 약간 다를 수 있습니다. 추적 헤더, malloc 내부 관리 정보와 프로세스 working set은 제외합니다. setup_live/operation_peak/final_live를 구분하며 최종 보유량이 아니라 작업 중 최대량을 비교합니다. 범위 입력 vector는 양쪽 조건 모두 setup에 포함됩니다.

메모리 측정은 각 조건을 한 번 실행하고 해제 후 추적 바이트가 0인지 검사했습니다. 시간 표본마다 같은 메모리 값을 반복 기록한 것을 독립 메모리 표본 9개로 해석하지 않습니다.

## 측정 방법과 범위

Intel i7-1165G7, x64 MSVC 19.51, C++20, Release, AVX2입니다. 논리 CPU 하나(affinity=1), BelowNormal, 빌드/실행 병렬 작업 1개와 /cgthreads1, /CGTHREADS:1을 적용했습니다. list<uint64_t>는 88바이트, iterator는 24바이트로 측정됐습니다. 타입과 iterator의 크기를 줄인 변경은 아닙니다.

각 조건에서 두 구현을 워밍업한 뒤 9개 시간 표본을 수집합니다. 표본마다 A/B 순서를 교대하며 시간은 표본당 여러 작업의 평균입니다. fixture 준비, 결과 체크섬 계산과 해제는 측정 시간에서 제외합니다. 읽기 대조군은 준비한 목록을 읽는 한 번의 작업 시간이며 반복 읽기의 캐시 최적 상태를 보장하지 않습니다. 모든 표본의 체크섬을 메모리 측정/워밍업과 비교합니다.

개별 후보의 메모리 측정에는 동일한 전역 할당 추적 래퍼가 양쪽에 남습니다. 최종 native 힙 비교는 NOVA_LIST_MEMORY_NATIVE_HEAP=ON으로 래퍼를 컴파일에서 제거합니다. 이 모드의 메모리 열은 0이며 메모리 절감량으로 해석하지 않습니다. 시스템의 다른 작업에 따른 시간 편차가 있어 무조건적인 성능 무저하를 보장하지 않습니다.

## 미채택: 디렉터리 제자리 재배치

고정 길이 큐의 같은 크기 디렉터리를 제자리 재배치하는 후보는 별도로 구현하고 검증했습니다. BlockBytes=64, 유지 길이 256, 100,000회 갱신 조건에서 최고 요청량이 uint64_t에서 6144 -> 5120B, record64에서 49166 -> 40974B로 감소했습니다.

하지만 native 힙의 반대 순서 재측정에서 뒤쪽 큐 갱신 중앙값은 짧은 string에서 4952.64 -> 5442.43us(+9.9%), record64에서 11010.28 -> 11325.06us(+2.9%)였습니다. 다른 조건은 감소했고 표본 편차도 있으므로 확정적인 회귀 원인으로 단정하지 않습니다. 다만 메모리 절감에 비해 성능 유지 근거가 부족하여 최종 코드에서 제외했습니다. 디렉터리 구현은 원본으로 되돌렸습니다.

rejected3_native_screen_100k는 세 후보를 합친 탐색 실행입니다. 일부 구간에 추가 후보 빌드가 겹쳤으므로 성능 채택 판단에서 제외합니다. 후보3의 독립 native_reverse 결과와 구분합니다.

## 검증과 재현

회귀 테스트는 개수 삽입의 내부 참조/앞/중간/뒤/빈 삽입/복사 예외 롤백, 분리 및 optimize 상태의 자기 범위 삽입, 이동 전용 범위 삽입, 혼합 소유권의 양방향 큐를 추가로 검사합니다.

기능 검증 명령은 .\run_list_harness.ps1입니다. ASan 구성은 .\run_list_harness.ps1 -BuildDirectory build/list-asan이며 기존 ASan CMake 설정을 사용합니다. ASan의 첫 실행은 DLL 검색 경로 누락으로 0xc0000135가 발생했고, 해당 VS 런타임 디렉터리를 실행 프로세스의 PATH에 추가해 해결했습니다. 라이브러리 동작 실패로 분류하지 않습니다.

A/B는 run_list_memory_benchmark.ps1을 사용합니다. BaselineHeader에는 nova_baseline 네임스페이스와 고유 include guard로 격리한 이전 헤더를 지정합니다. CandidateHeader를 생략하면 현재 헤더를 사용합니다. 예:

```powershell
.\run_list_memory_benchmark.ps1 -BaselineHeader C:/temp/list_baseline.hpp `
    -Label final_native_100k -Count 100000 -Iterations 3 -Group all -NativeHeap
```

Layout은 fragmented 또는 contiguous이며, 후자는 fixture 준비 단계에서 optimize합니다. Group은 all/count/range/input/queue/control입니다. ReverseOrder는 첫 측정 순서를 뒤집으며 내부 교대는 유지합니다. 빌드와 측정은 직렬 실행합니다. 결과 JSON에 각 헤더와 하니스 SHA-256, 조건과 실행 상태를 기록합니다.

초기 검토와 보류 근거는 [후보 문서](list_memory_candidates.md), 원자료는 이 문서와 같은 날짜의 docs/benchmarks/list_memory_* 파일에 보존합니다. 최종 성능 및 검증 결과는 아래 후속 기록에 기재합니다.

## 최종 결과

최종 두 후보만 포함한 코드에서 일반 Release 테스트 2/2, Release AddressSanitizer 테스트 2/2를 통과했습니다. UTF-8 BOM, CP949 표현 가능 문자와 기존 CRLF를 고려한 공백 검사를 통과했습니다. 메모리 추적, 워밍업, 시간 표본 모두 체크섬 검사를 통과했습니다. 상세 실행 상태와 소스 해시는 [검증 기록](benchmarks/list_memory_2026-11-04_validation.json)에 있습니다.

native 힙의 10만 개 삽입은 첫 순서를 바꾼 두 실행 모두 네 타입의 앞/중간/뒤 개수 삽입 및 범위 삽입에서 중앙값이 감소했습니다. 다음은 반대 순서 실행의 뒤 삽입이며 단위는 ms입니다. 각 값은 9개 표본의 중앙값이고 표본당 3회 평균입니다. 작업마다 준비/해제는 시간에서 제외합니다.

| 타입 | 개수 삽입 전 | 개수 삽입 후 | 범위 삽입 전 | 범위 삽입 후 |
|---|---:|---:|---:|---:|
| uint64_t | 0.792 | 0.588 | 1.067 | 0.616 |
| record64 | 4.532 | 3.297 | 14.337 | 6.922 |
| 짧은 string | 3.724 | 1.851 | 8.339 | 3.006 |
| 긴 string | 15.251 | 12.156 | 33.447 | 25.573 |

128개 삽입의 소량 조건도 별도로 측정했습니다. 작업별로 측정 함수를 분리한 하니스에서 9개 표본, 표본당 30회 평균을 비교했으며 네 타입의 앞/중간/뒤 개수/범위 삽입 24개 조건 모두 중앙값이 감소했습니다. 10만 개와 128개 이외의 모든 크기나 사용자 타입에서 같은 개선율을 보장하지 않습니다.

기본 작업 대조군은 분리 저장/optimize 후 연속 저장에서 정방향, 역방향, 인덱스, 정렬, optimize를 측정했습니다. 대조군의 wall-clock에는 증가와 감소가 섞여 있습니다. 예를 들어 분리 저장 uint64_t 정렬은 2.351 -> 2.784ms(+18.4%), 연속 저장은 +4.5%였고, 일부 짧은 작업은 수백 ns 차이에도 큰 비율이 나옵니다. 대조군의 감소를 이번 변경의 성능 개선으로 계산하지 않으며, 전체 작업에서 시간 무저하가 실측으로 확정됐다고 주장하지 않습니다.

이 편차를 추가 확인하기 위해 두 헤더를 같은 nova 네임스페이스로 각각 독립 컴파일했습니다. uint64_t 정/역/인덱스 순회, 정렬, optimize, 양방향 큐를 사용하는 [독립 probe](benchmarks/list_memory_2026-11-04_control_codegen.cpp)의 전체 어셈블리는 파일명/행 번호 주석만 달랐습니다. 이 주석을 제외한 내용은 SHA-256까지 동일했습니다. 이번 두 삽입 변경이 해당 probe의 실제 생성 명령을 바꾸지 않았음을 확인한 것이며, 다른 타입/사용 코드까지 동일하다는 보장은 아닙니다. [명령과 해시](benchmarks/list_memory_2026-11-04_codegen.json)를 보존했습니다.

## 원자료와 이전 코드 복원

- [후보1 메모리/시간](benchmarks/list_memory_2026-11-04_candidate1.csv), [후보2](benchmarks/list_memory_2026-11-04_candidate2.csv)
- [미채택 후보3](benchmarks/list_memory_2026-11-04_candidate3.csv), [후보3 native 반대 순서](benchmarks/list_memory_2026-11-04_candidate3_native_reverse.csv)
- [최종 native 10만 개](benchmarks/list_memory_2026-11-04_final_native_100k.csv), [반대 순서](benchmarks/list_memory_2026-11-04_final_native_100k_reverse.csv)
- [분리 저장 대조군](benchmarks/list_memory_2026-11-04_final_controls_fragmented.csv), [연속 저장 대조군](benchmarks/list_memory_2026-11-04_final_controls_contiguous.csv), [128개 소량 조건](benchmarks/list_memory_2026-11-04_final_native_small.csv)
- [전체 중앙값 요약](benchmarks/list_memory_2026-11-04_summary.csv): 탐색/미채택 데이터도 포함하므로 dataset 열로 구분합니다.

[최종 헤더 패치](benchmarks/list_memory_2026-11-04.patch)를 별도 복사본에 역적용하면 변경 전 구현을 얻을 수 있습니다. baseline용으로 고유 include guard와 nova_baseline 네임스페이스를 적용합니다. 실제 작업 트리에 역적용하지 않습니다. [미채택 후보3 패치](benchmarks/list_memory_2026-11-04_rejected3.patch)는 원자료 보존용이며 최종 코드에 적용하지 않습니다.

측정 중 대조군 분석을 위해 하니스의 작업 본문을 operation별 템플릿 함수로 분리했습니다. 최종 대조군/소량 측정은 분리한 버전이고, 개별 후보 및 첫 native 대량 삽입 기록은 이전 버전입니다. [하니스 패치](benchmarks/list_memory_2026-11-04_harness.patch)를 별도 복사본에 역적용하면 이전 버전을 복원합니다. UTF-8 BOM/CRLF 저장 시 이전 하니스 SHA-256은 5559DF7BDD5F2D91A9B5C957201250AA1594E202F0C5C06F9C8C7F71C2382329로 원자료 메타데이터와 일치함을 확인했습니다.
