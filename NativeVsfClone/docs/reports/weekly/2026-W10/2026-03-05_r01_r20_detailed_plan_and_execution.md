# R01-R20 Detailed Plan and Execution Report (2026-03-05)

## Summary

요청에 따라 `R01~R20` 재추진을 위해 상세 실행 계획을 수립하고, 이번 라운드에서는 우선순위 Top5(`R01/R02/R03/R05/R13`)를 코드로 추가 고도화했다.

적용 범위:

- `HostCore` 기능 확장
- `WpfHost` 운영 UI 확장
- 기존 R01-R20 MVP 기반 위에 실행 가능성/운영성 보강

## Detailed Execution Plan (This Round)

1. `R01/R03`:
   - preflight 결과에 실패 remediation를 직접 노출
   - 오류 발생 시 사용자 행동 가이드를 즉시 표시
2. `R02/R13`:
   - 임포트 가이드를 포맷별 fallback 메시지까지 확장
   - 로드 비동기 진행률/취소/타임아웃 가시화 추가
3. `R05`:
   - 자동 품질 임계치 하드코딩 제거
   - 정책값(프레임 임계, 연속 프레임 수, 쿨다운)을 설정/저장 가능하게 변경
4. 실행 검증:
   - HostCore/WPF 빌드 통과 확인

## Implemented Changes

- `host/HostCore/PlatformFeatures.cs`
  - 추가 타입:
    - `LoadProgressState`
    - `AutoQualityPolicy`
    - `AutoQualityPolicyStore`
- `host/HostCore/HostController.MvpFeatures.cs`
  - `LoadProgressChanged` 이벤트 추가
  - `LastUserFacingError` 상태 추가
  - `GetLastErrorGuidance()` 추가
  - `GetAutoQualityPolicy()`, `ConfigureAutoQualityPolicy(...)` 추가
  - `LoadAvatarAsync(...)` 단계별 진행률 publish (`queued/validating/loading/finalizing/completed`)
  - `CancelLoadAvatar()` 진행 상태 연동
  - `BuildImportGuidance(...)`에 파일존재/폴백 메시지 강화
  - `BuildUserFacingError(...)`에 source 기반 안내 강화
  - 자동품질 가드가 정책값(`AutoQualityPolicy`)을 사용하도록 변경
- `host/WpfHost/MainWindow.xaml`
  - Avatar 섹션: `Cancel Load`, 타임아웃 입력, 로드 진행률 바/텍스트 추가
  - Platform Ops 섹션: preflight 힌트 텍스트 추가
  - Platform Ops 섹션: auto quality policy 입력/적용 버튼 추가
- `host/WpfHost/MainWindow.xaml.cs`
  - `LoadProgressChanged` 이벤트 구독/표시
  - `CancelLoad_Click` 추가
  - `Load_Click` 타임아웃 입력 반영 + 로딩 중 UI 상태 제어
  - preflight 실패 remediation를 사용자에게 노출
  - 오류 발생 시 `GetLastErrorGuidance()`를 `QuickStatus`에 반영
  - auto quality policy 입력값 적용 핸들러 추가
  - 세션 초기화 시 auto quality policy 값을 UI에 반영

## Verification

검증 명령:

```powershell
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore
```

이번 라운드 결과:

- `HostCore`: PASS
- `WpfHost`: PASS

## Next Follow-ups

1. WinUI에도 동일한 로드 진행률/취소/preflight remediation UX 이식
2. `R15` Unity validator에 섹션 단위 assertion 추가
3. auto quality policy를 preset과 연동해 프로필별 정책 템플릿 제공
