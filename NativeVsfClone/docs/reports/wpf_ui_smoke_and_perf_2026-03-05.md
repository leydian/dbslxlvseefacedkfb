# WPF UI Smoke + Performance Evidence (2026-03-05)

## Purpose

Capture execution evidence for the WPF UI refresh-throttle change set and define the remaining manual verification scope.

Related implementation report:

- `docs/reports/ui_wpf_refresh_throttle_2026-03-05.md`

## Build Verification

Executed commands:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1
```

Result:

- Host publish mode: `WPF_ONLY`
- Native build (`nativecore`): PASS
- WPF publish output: PASS (`dist/wpf/WpfHost.exe`)

## Runtime Smoke Scope (Defined)

Target flow:

1. Initialize
2. Load avatar
3. Render steady-state (~30s)
4. Start/Stop Spout
5. Start/Stop OSC
6. Resize stress
7. Shutdown

## Runtime Smoke Status (Current Round)

- Non-interactive launch smoke: FAIL (latest run)
  - command shape:
    - launch `dist/wpf/WpfHost.exe` with working directory set to `dist/wpf`
    - verify process remains alive for 6 seconds
  - observed:
    - process exited early with code `-532462766` (unhandled runtime exception path)
  - event-log evidence (`Application` log):
    - `.NET Runtime` event `1026`
    - unhandled exception: `System.DllNotFoundException`
    - call path includes:
      - `MS.Internal.WindowsBase.NativeMethodsSetLastError.SetWindowLongPtrWndProc(...)`
      - `MS.Win32.HwndSubclass.HookWindowProc(...)`
- Full operator interactive smoke (7-step flow): NOT EXECUTED IN THIS ROUND
  - reason:
    - current validation round was executed in CLI/headless context
    - no UI automation hook exists in `host/WpfHost/MainWindow.xaml.cs` for the full operator flow

## Performance Evidence (Current Round)

Policy baseline introduced by code:

- render loop remains 60Hz path (`_timer` at ~16ms)
- UI diagnostics/status refresh moved to 10Hz (`_uiRefreshTimer` at 100ms)
- logs text rebuild is log-tab-aware

Quantitative runtime metrics capture status:

- `LastFrameMs` before/after table: NOT COLLECTED IN THIS ROUND
  - manual operator-run telemetry capture is still required for true before/after numbers
- Logs tab active vs inactive comparative observation: CODE-PATH VERIFIED
  - `DiagnosticsTabControl` selection state is used as the active-tab gate
  - `LogVersion`/`SnapshotVersion` checks are wired to prevent redundant text rebuilds

## Pipeline Verification Snapshot (Latest Round)

Artifacts and outcomes:

- `build/reports/host_publish_latest.txt`
  - generated: `2026-03-05T16:42:47.5904580+09:00`
  - `HostPublishMode: WPF_PLUS_WINUI`
  - WPF publish: PASS, WinUI publish: FAIL (`TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`)
- `build/reports/vsfavatar_gate_summary.txt`
  - generated: `2026-03-05T16:57:03`
  - `Overall: PASS`
  - `HostTrackStatus: PASS_WPF_BASELINE`
- `build/reports/quality_baseline_summary.txt`
  - generated: `2026-03-05T16:49:46`
  - `Overall: PASS`

## Acceptance Gate for Closure

This evidence document is considered complete when:

1. WPF publish/gate/baseline rerun evidence is attached with PASS outcomes
2. non-interactive launch smoke evidence is attached (pass or fail must be recorded explicitly)
3. remaining manual-only evidence is explicitly listed as deferred, not left as `PENDING`

## Notes

- No behavior change is intended for native render cadence; this round focuses on reducing managed UI refresh churn.
- WinUI parity is intentionally out of scope for this artifact and tracked separately.
- Deferred manual evidence:
  - full 7-step interactive operator smoke
  - quantitative `LastFrameMs` before/after measurement table
- Additional open item from latest run:
  - investigate `DllNotFoundException` root dependency chain behind launch exit code `-532462766` in current CLI/headless environment

## Status Board Link

- consolidated tracker: `docs/reports/host_blocker_status_board_2026-03-05.md`

## Follow-up Update (2026-03-05, post-automation rerun)

- executed direct smoke probe using `tools/wpf_launch_smoke.ps1` against `dist/wpf/WpfHost.exe`
- latest artifact:
  - `build/reports/wpf_launch_smoke_latest.txt`
  - `WPF launch smoke run: 2026-03-05T17:25:43.9247110+09:00`
  - `Status: FAIL`
  - `ExitCode: -532462766`
- note:
  - this run did not capture a fresh matching Application event within the script window
  - historical evidence from same-day runs still shows `.NET Runtime` event `1026` with `System.DllNotFoundException`

## Follow-up Update (2026-03-05, repeat smoke verification)

- repeated direct smoke probe twice with same execution shape:
  - `runA`: `2026-03-05T18:17:49.3166582+09:00`
  - `runB`: `2026-03-05T18:18:02.8195550+09:00`
- both runs:
  - `Status: FAIL`
  - `ExitCode: -532462766`
- evidence files:
  - `build/reports/wpf_launch_smoke_runA.txt`
  - `build/reports/wpf_launch_smoke_runB.txt`

## Follow-up Update (2026-03-05, startup-crash fix verification)

- root cause refinement:
  - latest event-log evidence showed startup failure was `NullReferenceException` in `MainWindow.RefreshValidationState()` rather than `DllNotFoundException`.
  - exception anchor:
    - `WpfHost.MainWindow.RefreshValidationState()` (`MainWindow.xaml.cs:645` in failing build)
- fix applied:
  - `host/WpfHost/MainWindow.xaml.cs`
    - added UI-ready gating for early `TextChanged` events during XAML initialization.
    - added null guards for validation controls before calling `_controller.ValidateInputs(...)`.
- verification:
  - `publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi` rerun:
    - WPF publish: PASS
    - WPF launch smoke: PASS
  - direct smoke rerun:
    - `build/reports/wpf_launch_smoke_latest.txt`
    - `WPF launch smoke run: 2026-03-05T21:36:16.2937824+09:00`
    - `Status: PASS`
    - `ExitCode: 0`
- current status:
  - non-interactive WPF launch smoke blocker is closed in this environment.
