# 변경 내역

## 미출시

- 모든 컨테이너의 공개 API, 내부 구현, 테스트와 벤치마크 명명을 `snake_case`로 통일했습니다. PascalCase 중복 API·타입 별칭을 제거하고 집계 헤더를 `NovaContainers/nova_containers.hpp`로 변경했습니다. 기존 PascalCase 호출과 집계 헤더 include는 수정해야 합니다.

- `list::optimize` / `optimize`와 연속 상태 조회·span API를 추가했습니다. 연속 메인 배열과 양끝 추가 블록을 함께 관리하고 일반 iterator 및 인덱스 접근에 연속 경로를 제공합니다.

- `list::for_each` / `for_each`에 블록별 연속 순회 경로를 추가하고 상태 반환·const·이동 전용 callable·예외 전파를 검증했습니다. 일반 iterator 변경 후보는 회귀로 미채택했으며 동일 프로세스 교대 A/B benchmark와 실험 기록을 추가했습니다.

- 블록형 `nova::list`를 추가했습니다. 양끝 확장, 임의 접근, 중간 및 범위 변경, 표준 정렬과 블록별 span 순회를 지원합니다.
- 원소 주소 안정성, allocator 전파 및 예외 처리를 포함하는 기능 테스트와 vector/deque 비교 benchmark를 추가했습니다.
- 속도 우선 설계와 iterator 무효화 계약, 검증 결과는 `docs/list_design.md`에 기록합니다.

## 1.1.0 - 2026-10-23

- 내부 구현을 `detail/hash/`, `detail/flat/`으로 재구성했습니다.
- 공통 이종 키 조회 제약, 키 삭제, 해시 후보 비교 및 저장소 복사를 정리했습니다.
- `flat_map` proxy iterator를 독립 헤더로 분리하고 호환성 테스트를 추가했습니다.
- 기능 테스트를 hash/flat 계열로 구분하고 benchmark를 `benchmarks/`로 이동했습니다.
- CMake 개발 실행 파일 설정을 공통화하고 소비 프로젝트로 전파되는 경고 옵션을 제거했습니다.
- 하위 프로젝트로 포함할 때 테스트와 benchmark는 기본 비활성화됩니다. 명시적으로 설정한 옵션은 유지됩니다.
- 공개 헤더 경로와 API 이름은 유지됩니다. `detail/`의 직접 include 경로와 `flat_map` iterator의 내부 타입 식별은 변경되었습니다.
- benchmark 실행 파일 위치가 `build/tests/`에서 `build/benchmarks/`로 변경되었습니다.
- 내부 설계 문서와 README를 갱신했습니다.

### 검증

코드와 참조 경로를 직접 검토했습니다. 빌드, 테스트 및 정적 분석은 실행하지 않았습니다.
