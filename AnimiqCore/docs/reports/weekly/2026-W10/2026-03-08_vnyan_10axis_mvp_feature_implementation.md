# 2026-03-08 VNyan 10-Axis MVP Feature Implementation

## Summary

이번 라운드에서는 VNyan 벤치마킹 10축 중 제품화 우선도가 높은 항목들을 Animiq HostCore/WPF에 MVP 기능으로 실제 반영했다.

핵심 구현 결과:

- 자동화 워크플로우 트리거/액션 확장
- 커맨드 기반 트리거 실행 경로 추가
- Spout Receiver 자동 재연결 옵션 추가
- WPF 운영 패널에 템플릿 삽입/Quick Preset/Panic Stop/Recovery 액션 추가
- 렌더 위 오버레이 아이템 표시 경로 추가
- 확장 포인트(Automation Extension Registry) 기본 계약 추가

## Implemented Changes

### 1) Workflow Node/Action 확장

`host/HostCore/AutomationWorkflow.cs`

- 신규 Trigger:
  - `CommandTrigger`
- 신규 Action:
  - `SendOscAction`
  - `SetExpressionBatchAction`
  - `SpawnOverlayItemAction`
  - `ClearOverlayItemsAction`
  - `ExtensionAction`
- 그래프 로딩 시 `ValidateGraph(...)` 추가:
  - 중복 node id 차단
  - edge source/target 누락 차단
  - nodes/edges 필수 스키마 검증
- command queue 경로 추가:
  - `EnqueueCommand(...)`
  - tick 시 command drain 후 trigger 평가

### 2) HostController Automation 실행 경로 확장

`host/HostCore/HostController.Automation.cs`

- 신규 확장 포인트:
  - `AutomationExtensionRegistry` 연동
  - `RegisterAutomationExtension(...)`
  - `GetAutomationExtensionIds()`
- 신규 runtime 진입점:
  - `TriggerAutomationCommand(...)`
- Spout Receiver:
  - `SetSpoutReceiverAutoReconnect(...)`
  - tick 시 자동 재연결(`TryAutoReconnectSpoutReceiver`)
- 신규 액션 핸들러:
  - OSC 전송
  - 배치 표정 설정
  - 오버레이 아이템 spawn/clear command enqueue
  - extension action dispatch
- SwapAvatar 액션 옵션:
  - `pre_delay_ms`, `post_delay_ms`, `fallback_path`, `preserve_outputs`

### 3) Automation Extension 계약 추가

`host/HostCore/AutomationExtensions.cs` (new)

- `IAutomationExtension`
- `AutomationExtensionRegistry`

목적:

- 외부/내부 확장 액션을 안정적으로 등록/실행할 최소 계약을 제공

### 4) WPF 운영 UI/단축키/오버레이 기능 추가

`host/WpfHost/MainWindow.xaml`
`host/WpfHost/MainWindow.xaml.cs`

- Operations > Automation:
  - command 입력 + 실행 버튼
  - 템플릿 선택/삽입 UI
  - Quick Preset 버튼 3종
  - Panic Stop 버튼
  - Recovery 버튼 3종
- 렌더 영역:
  - `RuntimeOverlayCanvas` 추가 (렌더 상단 표시용)
- 글로벌 단축키:
  - `Ctrl+Shift+X` -> Panic Stop
- 오버레이 렌더 루프:
  - tick마다 overlay command 처리
  - duration 기반 자동 제거
- Automation status 텍스트 확장:
  - receiver auto-reconnect 상태 노출

## Verification

실행한 검증:

- `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore`
- `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore`

결과:

- HostCore: PASS
- WpfHost: PASS

참고:

- 병렬 빌드 시 일시적 파일 잠금(CS2012) 1회 발생했고, 순차 재실행으로 정상 통과했다.

