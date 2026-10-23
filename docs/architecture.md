# 내부 구조와 변경 원칙

## 공개 계층

`hash_map.hpp`, `hash_set.hpp`, `flat_map.hpp`, `flat_set.hpp`는 공개 타입, 생성자, API 전달과 iterator 노출을 담당합니다. 소문자 API는 PascalCase 구현에 위임합니다. `nova::containers`의 타입과 `nova`의 별칭을 모두 유지합니다.

map과 set은 동일 계열의 저장 코어를 공유합니다. set은 const iterator만 노출하고, hash map은 `pair<const Key, Value>`를 저장합니다. flat map은 정렬 및 이동을 위해 `pair<Key, Value>`를 저장하되 proxy iterator로 key 수정을 막습니다. 이 차이 때문에 공개 계층 전체를 하나의 상속 기반 구현으로 합치지 않습니다.

## 공통 계층

`detail/concepts.hpp`는 hash, equality, comparator 제약과 transparent 이종 키 조회 조건을 정의합니다. 일반 key overload는 유지하여 기존 암시적 변환을 허용하며, 별도 이종 키 overload만 공통 concept으로 제한합니다.

`detail/key_extractors.hpp`는 pair의 첫 필드 또는 값 자체에서 key를 추출합니다. key 기반 삭제는 각 저장 코어의 `EraseKey()`가 담당하고, iterator 삭제와 반환 타입은 기존 공개 API를 유지합니다.

## Hash 계층

`detail/hash/raw_hash_table.hpp`는 슬롯 할당, 원소 수명, control byte, 탐색, 재해시를 소유합니다. `simd_group.hpp`는 SIMD group과 bit mask 연산만 담당합니다.

조회와 삽입은 group 후보의 key 비교를 공유하지만 종료 조건은 각각 유지합니다. 조회는 empty group에서 실패하고, 삽입은 tombstone 후보를 기억한 뒤 empty group까지 중복 key를 확인합니다. control 배열의 tail은 앞 group을 복제하여 wrap-around 탐색을 지원합니다.

복사 생성과 복사 대입은 저장소 복사 경로를 공유합니다. 복사 생성자의 예외 정리, 대입의 allocator 전파 결정, replacement 교환 순서는 각 호출자가 담당합니다. 재해시의 copy/move 선택과 allocator가 다른 이동 대입은 의미가 달라 별도 경로로 유지합니다.

## Flat 계층

`detail/flat/flat_table.hpp`는 vector와 comparator를 소유하며 정렬 상태, 이진 탐색, 삽입 및 삭제를 담당합니다. 일괄 구성은 같은 allocator의 임시 저장소에서 검증을 마친 뒤 교환합니다.

`detail/flat/map_iterator.hpp`는 flat map의 proxy reference와 random-access 연산을 담당합니다. 실제 저장 iterator의 생성 및 접근은 소유 map과 iterator 특수화 사이에서만 허용합니다. `FlatMap::BasicIterator`, `iterator`, `const_iterator` 이름은 별칭으로 유지합니다.

## 개발 구성

`cmake/NovaDevelopment.cmake`가 자체 실행 파일의 공통 옵션을 설정합니다. 라이브러리 소비자에는 C++20, include 경로, AVX2 옵션만 전파합니다. 테스트 target 및 CTest 이름은 유지하며, benchmark 출력 디렉터리는 `build/benchmarks/`로 분리합니다.

리팩터링 확인 시 기존 hash 충돌, tombstone, allocator 소유권, 예외, flat 정렬, 이종 키 조회, iterator 테스트가 관련 범위입니다. 빌드와 테스트 실행은 별도 요청이 있을 때 수행합니다.
