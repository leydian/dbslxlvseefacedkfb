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
