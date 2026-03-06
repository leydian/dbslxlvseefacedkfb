# 2026-03-06 Tracking Stability Hysteresis and Auto Tuning

## Summary
This update refines camera-tracking stability with a focus on long-run reliability over raw responsiveness.

Key outcomes:
- reduced source ping-pong in `HybridAuto` through hysteresis/cooldown/hold-time rules,
- softer upper-body decay under stale/low-confidence input,
- runtime deadband auto-tuning based on instability signals,
- and richer operator diagnostics for source switching behavior.

## Problem
- In unstable environments, `HybridAuto` could switch sources too frequently when packet age/failure edges oscillated.
- Upper-body motion could twitch when confidence dropped suddenly or source freshness degraded.
- Operators lacked immediate visibility into switch dynamics (why/when/cooldown), making triage slower.

## Implementation

### 1) Stability contract expansion
Files:
- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/PlatformFeatures.cs`

Added:
- `TrackingStartOptions.AutoStabilityTuningEnabled`
- `TrackingInputSettings.AutoStabilityTuningEnabled`
- `TrackingDiagnostics` fields:
  - `RecentSourceSwitchCount`
  - `LastSourceSwitchReason`
  - `SourceSwitchCooldownRemainingMs`

Session model updated:
- persistence version bumped `9 -> 10`
- migration default for pre-v10 sessions:
  - enable auto stability tuning only when source type is `HybridAuto`.

### 2) HostController wiring
Files:
- `host/HostCore/HostController.cs`
- `host/HostCore/HostController.MvpFeatures.cs`

Changes:
- start-tracking path now forwards `AutoStabilityTuningEnabled` to runtime options.
- tracking config save/update path supports this field and logs it in `TrackingConfig`.

### 3) Hybrid source arbitration hardening
File:
- `host/HostCore/TrackingInputService.cs`

Refinements:
- added source-switch state tracking (`last switch time`, `windowed switch count`, `switch reason`, `cooldown`).
- fallback/recovery now consider:
  - confidence gates (ifacial/webcam),
  - minimum active-source hold time,
  - cooldown window after switch,
  - and dynamic failure threshold behavior for stability mode.
- source switches are reason-tagged:
  - `fallback:webcam`
  - `recover:ifacial`

### 4) Runtime auto-stability tuning
File:
- `host/HostCore/TrackingInputService.cs`

Behavior:
- computes instability score from:
  - parse error ratio,
  - drop ratio,
  - recent source switch rate.
- adjusts effective pose deadband with EMA toward a target based on instability band.
- when disabled or not in `HybridAuto`, deadband remains anchored to configured value.

### 5) Upper-body jitter reduction
File:
- `host/HostCore/TrackingInputService.cs`

Improvements:
- stale upper-body decay now uses gentler profile in stability mode.
- low-confidence upper-body input applies additional soft decay to suppress twitching.
- confidence decay factor tuned for less abrupt visual collapse.

### 6) Operator diagnostics and hinting
Files:
- `host/HostCore/TrackingErrorHintCatalog.cs`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

Added:
- new warning hint `TRACKING_SOURCE_SWITCH_THRASH` with actionable guidance.
- status text now surfaces switch telemetry:
  - `switches`
  - `switch_reason`
  - `switch_cd_ms`

## WPF UI changes
Files:
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

Added:
- new tracking checkbox: `TrackingAutoStabilityCheckBox` (`자동 안정화 튜닝`).

Wiring:
- included in start-tracking config apply path,
- persisted via existing tracking session settings,
- restored on startup,
- disabled while tracking is active (consistent with other tracking settings).

## Validation
- `HostCore` release build: PASS
- `WpfHost` release build: PASS
- `WinUiHost` release build: known WinUI XAML compiler failure path remained (`XamlCompiler.exe` MSB3073), unrelated to the C# contract wiring in this change set.

## Expected Impact
- fewer rapid source oscillations in marginal network/capture conditions,
- smoother upper-body behavior during confidence drops,
- faster operator diagnosis for switching instability and transient fallback loops.
