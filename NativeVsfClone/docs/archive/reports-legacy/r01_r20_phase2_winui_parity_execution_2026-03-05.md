# R01-R20 Phase2 WinUI Parity Execution Report (2026-03-05)

## Summary

`R01~R20` production-ready 전개의 2차 실행으로, WPF에서 먼저 적용한 운영 기능을 WinUI host에 패리티로 이식했다.

이번 라운드 핵심은 아래 항목의 WinUI 정합성 강화다:

- `R01` preflight 실행/가이드 노출
- `R02` 포맷 임포트 진입성 개선(`.xav2` 포함)
- `R03` 오류 가이드 노출 강화
- `R05` auto-quality policy 적용 UI
- `R13` async load 진행률/취소/timeout UX
- `R17/R20` track status/guide 탭 가시화

## Implemented Changes

Updated:

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

### UI additions (WinUI)

- Avatar 섹션:
  - `Cancel Load` 버튼
  - `Load timeout(ms)` 입력
  - load progress bar + stage text
- Platform Ops 섹션(신규):
  - preflight 실행
  - diagnostics/metrics export
  - render profile buttons(quality/performance/stability)
  - sidecar settings(mode/path/timeout/strict) + apply
  - telemetry(opt-in/redact/export)
  - auto-quality policy(threshold/consecutive/cooldown) + apply
- Diagnostics 탭:
  - `Guides` 탭 추가(quickstart + compatibility/fallback)
- Status strip:
  - `TrackStatus` 추가(`WPF/WinUI` 상태 가시화)

### Code-behind wiring

- `LoadProgressChanged` 이벤트 구독 및 UI 반영
- `Load_Click`를 async/timeout 기반으로 전환
- `CancelLoad_Click` 추가
- preflight remediation 문구(`PreflightHintText`) 노출
- `GetLastErrorGuidance()` 결과를 상태 안내에 반영
- auto-quality policy 파싱/적용 핸들러 추가
- session default(사이드카/정책) UI hydrate
- 파일 선택 필터에 `.xav2` 추가

## Verification

Executed:

```powershell
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore
dotnet build NativeVsfClone\host\WinUiHost\WinUiHost.csproj -c Release -p:Platform=x64 --no-restore
```

Result:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL (existing environment/toolchain blocker)
  - `MSB3073`
  - `XamlCompiler.exe` execution failure path

## Notes

- WinUI 실패는 기존에 추적 중인 XAML 컴파일러 블로커 경로와 동일하다.
- 이번 변경은 WinUI 코드 패리티를 높였고, blocker 해소 시 즉시 기능 검증 가능한 상태로 정리되었다.
