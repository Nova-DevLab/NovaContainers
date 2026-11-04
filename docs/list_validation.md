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
