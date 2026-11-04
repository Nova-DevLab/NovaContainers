# nova::list Release 검증

코드 변경 후 저장소 루트에서 다음 명령을 실행합니다.

```powershell
.\run_list_harness.ps1
```

별도 `build/list-release` 디렉터리에 Release로 구성한 뒤 list 기본 테스트와
`optimize()` 테스트만 증분 빌드하고 CTest로 실행합니다. 두 테스트는 Release에서도
검사하는 `require`를 사용합니다. 벤치마크나 Debug 빌드는 실행하지 않습니다.

빌드만 필요한 경우에는 다음 명령을 사용합니다. 이 결과는 실행 검증 성공을 뜻하지 않습니다.

```powershell
.\build_list.ps1
```

두 스크립트는 실행 위치와 무관하게 저장소 기준 경로를 사용합니다.
`-BuildDirectory`로 다른 빌드 경로를 지정할 수 있습니다.
빌드와 하니스는 `BelowNormal` 우선순위, 허용된 논리 코어 중 하나,
병렬 작업 1개로 실행합니다. MSVC 컴파일과 링크 병렬화도 제한합니다.
적용할 수 없으면 실패로 보고합니다. 호출 프로세스의 환경 변수와 우선순위,
affinity는 종료 시 복원합니다.

결과는 `<빌드 경로>/list-validation/summary.json`에 저장합니다.
단계별 `configure.log`, `build.log`, `harness.log`도 같은 폴더에 기록합니다.
요약의 `build`, `harness`가 모두 `passed`인지 확인하고 보고합니다.
빌드만 실행한 경우 `harness`는 `not_run`입니다. 각 로그는 해당 단계를 실행할 때
덮어쓰므로 현재 실행 결과는 항상 `summary.json`의 단계 목록을 기준으로 확인합니다.
전체 성공은 종료 코드 0, 실패는 1입니다. 테스트 미발견과 테스트별 300초 초과도 실패입니다.

작업 보고에는 변경 내용, Release 빌드 성공 여부, 하니스 통과 수와 실패 내용만
간결하게 기재합니다. 성능이나 메모리 개선 여부는 이 기능 검증 결과로 판단하지 않습니다.

## MSVC AddressSanitizer

설치된 x64 MSVC의 ASan 런타임이 있을 때 다음 명령을 사용합니다.

```powershell
.\run_list_harness.ps1 -AddressSanitizer
```

기본 빌드 경로는 일반 Release와 분리한 `build/list-asan`입니다. 사용자 지정 경로도 일반 빌드와 분리해서 사용합니다. Release 최적화에 `/EHsc /fsanitize=address /Zi`와 링크 `/INCREMENTAL:NO /DEBUG`를 적용합니다. 컴파일러 옆의 ASan DLL 경로와 ASAN_OPTIONS는 자식 실행에만 적용하고 종료 시 복원합니다. 이 옵션은 MSVC x64 전용입니다. 누수 진단을 지원하는 Linux LeakSanitizer나 UBSan 검증을 대신하지 않습니다.

## 최신 검증 기록 (2026-11-04)

- 직접 예약 삽입 헤더의 일반 Release CTest 2/2 통과 후, 같은 헤더에서 MSVC Release ASan CTest 2/2가 통과했습니다. ASan 오류 보고는 없었습니다.
- 두 하니스는 BelowNormal, affinity=1, 병렬 작업 하나로 실행했습니다. 체크 조건은 Release에서도 활성화되어 있습니다.
- 최초 ASan 설정에서 CMAKE_CXX_FLAGS를 지정하면서 MSVC 기본 `/EHsc`를 빠뜨려 객체 해제 테스트가 실패했습니다. 하니스 설정을 수정한 뒤 재빌드해 통과했습니다. 라이브러리 회귀로 판정하지 않았으며 라이브러리 소스는 이번 최종 검증에서 수정하지 않았습니다.
- 소스 SHA-256 및 실행 요약은 [최종 검증 자료](benchmarks/list_final_2026-11-04_validation.json)에 보존합니다. 상세 로그는 각 build 디렉터리의 list-validation 아래에 있습니다.
- 로컬의 확인한 설치 경로에 GCC/Clang이 없고 WSL에는 docker-desktop만 등록되어 있어 최신 변경의 다른 컴파일러 실행은 미검증입니다. 저장소의 Linux GCC/Clang 및 ASan+UBSan CI 구성은 이번 미커밋 변경으로 실행된 결과가 아닙니다.
- MSVC ASan은 테스트가 실행한 메모리 접근 오류 검증입니다. 이번 결과로 전체 경로의 무결성이나 누수 없음까지 확정하지 않습니다.
