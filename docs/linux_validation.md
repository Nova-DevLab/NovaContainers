# Linux 자동 검증

GitHub Actions의 `Linux CI`는 Ubuntu 24.04 x86-64에서 다음 세 구성을 차례로 실행합니다.

- GCC Release: 전체 테스트 및 벤치마크 타깃 빌드, CTest 5개.
- Clang Release: 전체 테스트 및 벤치마크 타깃 빌드, CTest 5개.
- Clang Release ASan+UBSan: 전체 테스트 빌드, CTest 5개와 누수·메모리 접근·정의되지 않은 동작 검사. 진단 발생 시 실패합니다.

성능 벤치마크는 빌드만 하고 실행하지 않습니다. 테스트 검사는 Release에서도 활성화됩니다. 모든 configure/build/test 프로세스는 nice=10, 사용 가능한 논리 CPU 하나, 병렬 작업 하나로 실행합니다. 행렬의 max-parallel=1 및 워크플로 동시 실행 제한도 적용합니다.

## 자동 및 수동 실행

푸시와 pull request는 해당 커밋을 검증합니다. 특정 릴리스는 Actions의 `Linux CI`에서 `Run workflow`를 선택하고 `source_ref`에 `v1.0.0` 같은 태그나 커밋 SHA를 입력합니다. 실행할 워크플로 브랜치는 `main`을 선택합니다. 입력을 비우면 실행 대상 커밋을 검증합니다.

워크플로는 main에서 읽되 소스 checkout은 지정한 ref를 사용하므로, 워크플로를 포함하지 않는 기존 릴리스 커밋도 변경 없이 검증할 수 있습니다. 로그의 `Source commit`으로 실제 검사한 SHA를 확인합니다. 게시한 태그를 이동할 필요가 없습니다.

## 결과 확인

저장소 Actions에서 세 작업의 성공 여부를 확인합니다. 실패 시 `Build and test on one logical CPU` 단계의 최초 컴파일 오류 또는 CTest 실패 출력을 확인합니다. 컴파일러 버전, 적용한 niceness와 affinity, 소스 SHA도 같은 로그에 기록합니다.

워크플로 파일은 `.github/workflows/ci.yml`이며 Git 추적 대상입니다. `.github`의 다른 로컬 파일은 기존처럼 제외합니다.
