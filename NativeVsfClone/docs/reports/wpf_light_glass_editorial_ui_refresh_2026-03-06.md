# WPF Light Glass Editorial UI Refresh (2026-03-06)

## Summary

`WpfHost` UI를 기존 단일 스크롤형 컨트롤 패널 중심 구조에서, 시각적 내비게이션 레일 + 작업 패널 기반의 현대적 레이아웃으로 개편했다.  
핵심 목표는 기능 로직 변경 없이 조작 밀도를 낮추고, 운영 화면의 가독성과 상태 인지성을 높이는 것이다.

## Scope

- 대상: `host/WpfHost`
- 비대상: `host/WinUiHost`, `HostCore`, native runtime
- 기능 계약: 기존 `x:Name`/이벤트 핸들러 유지(행동 회귀 최소화)

## Changed Files

- `host/WpfHost/App.xaml`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

## Detailed Changes

### 1) Design Token / Global Style System (`App.xaml`)

- 타이포 토큰 추가:
  - `Font.Body=Segoe UI Variable Text`
  - `Font.Display=Segoe UI Variable Display`
- 색상 토큰 체계화:
  - 표면/카드/보더/텍스트/강조/상태 브러시 (`Color.*`)
- 간격 토큰 추가:
  - `Spacing.ControlPadding`
  - `Spacing.InputPadding`
- 전역 컨트롤 스타일 개선:
  - `Window`, `GroupBox`, `Button`, `TextBox`, `ComboBox`, `ListBox`, `TabControl`, `TabItem`, `CheckBox`, `TextBlock`, `ProgressBar`
- 버튼 상호작용 상태 정리:
  - hover/pressed/disabled 시각 상태 분리

### 2) Layout Modernization (`MainWindow.xaml`)

- 루트 레이아웃 확장:
  - 좌측 컨트롤 영역 폭 `440 -> 560`
  - 스플리터 폭 `12 -> 14`
- 좌측 영역 재구성:
  - 신규 `LeftRailPanel` 추가(워크스페이스 내비게이션 시각 레일)
  - 우측 `ControlPanelScrollViewer`는 기존 기능 섹션(`GroupBox`) 유지
- 우측 프리뷰/상태 영역 스타일 개선:
  - 렌더 호스트 테두리/코너/톤 개선
  - 디버그 오버레이 패널 스타일 정리
  - 상태바 배경/텍스트 대비 상향

### 3) Render-only Visibility Sync (`MainWindow.xaml.cs`)

- `ApplyModeVisibility()` 업데이트:
  - render-only 진입 시 `LeftRailPanel` 숨김
  - 일반 모드 복귀 시 `LeftRailPanel` 표시
  - 새 레이아웃 폭 기준(`560/14`) 반영

## Behavior / Compatibility Notes

- 주요 버튼/입력/슬라이더/콤보의 이벤트 핸들러는 유지되어 동작 계약은 동일.
- UI 모드(beginner/advanced) 및 render-only(F11) 흐름은 기존 정책을 계승.
- 이번 변경은 시각 및 정보구조 개선 중심이며, API/파일포맷/런타임 계약 변경 없음.

## Verification

- Command:
  - `dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore`
- Result:
  - PASS (`0 warnings`, `0 errors`)

