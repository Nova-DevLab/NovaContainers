# list 직접 예약 삽입과 임시 vector A/B 검증

측정일: 2026-11-04.

현재 직접 예약 삽입은 10만 개 외부 범위의 네 타입/세 위치/두 실행 순서 24개 비교에서 모두 시간 중앙값이 감소했습니다. 임시 vector 원소 저장을 제거하여 최대 동시 요청 메모리도 감소했습니다. 아래 결과는 대상 컨테이너로서 std::vector와 비교한 것이 아니라, nova::list 범위 삽입의 임시 vector 방식과 직접 예약 방식을 비교한 것입니다.

## 10만 개 뒤 삽입

시간은 9개 교대 표본의 중앙값이며 각 표본은 3회 평균입니다. 시간 변화는 두 초기 실행 순서의 결과입니다. 메모리는 별도 요청 바이트 추적에서 한 번 측정했습니다.

| 타입 | vector 임시 버퍼 (ms), 정순/역순 | 직접 삽입 (ms), 정순/역순 | 시간 변화 | 최대 요청량 (B), 이전/현재 | 메모리 변화 | 작업 할당 횟수, 이전/현재 |
|---|---:|---:|---:|---:|---:|---:|
| u64 | 0.671 / 0.615 | 0.456 / 0.461 | -32.0% / -25.0% | 2,405,621 / 1,605,582 | -33.3% | 50 / 49 |
| record64 | 3.132 / 3.115 | 1.435 / 1.387 | -54.2% / -55.5% | 19,244,237 / 12,844,198 | -33.3% | 393 / 392 |
| string_short | 1.531 / 1.621 | 0.586 / 0.610 | -61.7% / -62.4% | 9,622,154 / 6,422,115 | -33.3% | 197 / 196 |
| string_long | 12.018 / 9.811 | 10.319 / 9.463 | -14.1% / -3.5% | 32,036,602 / 28,836,563 | -10.0% | 100197 / 100196 |

## 조건

- Intel i7-1165G7, MSVC 19.51, x64 Release C++20 AVX2. BelowNormal, affinity=1, 빌드 및 실행 병렬 작업 하나, /cgthreads1, /CGTHREADS:1.
- 기존 원소 128개에 외부 vector 입력 128개 또는 100000개를 앞/중간/뒤에 삽입합니다. BlockBytes=16384, fragmented 배치입니다. 자기 범위 및 큰 기존 목록/연속 배치는 이번 성능 측정 대상이 아닙니다.
- uint64_t 8B, record64 64B, string 객체 32B이며 짧은 문자열 6자/긴 문자열 96자입니다.
- 동일한 현재 헤더에서 범위 삽입 함수만 vector.reserve(count)로 임시 저장하도록 바꾼 기준본을 사용했습니다. allocator 한도 등 다른 구현은 동일합니다.
- NativeHeap 시간 실행 4회와 별도 메모리 추적 실행 2회가 성공했습니다. 준비/체크섬/파괴는 삽입 시간에서 제외합니다. 메모리/워밍업/시간 표본의 체크섬과 요청량 해제 검사가 통과했습니다.
- 최대 요청량은 입력 vector, 원소 블록, 임시 버퍼, 디렉터리, 문자열 저장소를 포함합니다. malloc 메타데이터와 추적 헤더, RSS/working set은 제외하므로 프로세스 메모리 감소율로 해석하지 않습니다.
- 128개도 표본당 3회로 측정했습니다. 작업 시간이 짧으므로 작은 차이는 확정하지 않습니다. 긴 문자열 중간 삽입은 정순 +3.7%, 역순 -12.6%로 방향이 달랐으며 그 밖의 128개 비교 23건은 중앙값이 낮았습니다.
- 이번 성능 측정에서 라이브러리는 수정하지 않았고 스냅샷과 현재 헤더 바이트가 일치합니다. 벤치마크 Release 빌드는 성공했으며 CTest는 재실행하지 않았습니다. 직전 기능 검증은 Release CTest 2/2 통과입니다.

## 재현 및 원자료

```powershell
.\run_list_memory_benchmark.ps1 -BaselineHeader docs/benchmarks/list_direct_2026-11-04_baseline.hpp -CandidateHeader docs/benchmarks/list_direct_2026-11-04_candidate.hpp -Label direct_native_100000_forward -Count 100000 -Iterations 3 -Group range -NativeHeap
```

역순은 -ReverseOrder를 추가하고 다른 Label을 사용합니다. 메모리는 -NativeHeap 없이 실행합니다. Count=128도 동일하게 측정합니다.

- [조건 및 소스 해시](benchmarks/list_direct_2026-11-04_manifest.json)
- [실행 검증 기록](benchmarks/list_direct_2026-11-04_validation.json)
- [모든 위치/타입의 시간 중앙값 및 메모리 요약](benchmarks/list_direct_2026-11-04_summary.csv)
- 원시 CSV/JSON/stderr: benchmarks/list_memory_2026-11-04_direct_*.
