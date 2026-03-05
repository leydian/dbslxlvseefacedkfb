# WinUI UI Refresh Throttle Parity Implementation (2026-03-05)

## Summary

Implemented the WinUI-side parity model for the WPF refresh-throttle pattern while keeping the WPF-first release policy unchanged.

Implemented direction:

- preserve render tick cadence at ~60Hz (`16ms`)
- move non-render UI refresh to 10Hz (`100ms`)
- switch from immediate full refresh to dirty-flag processing
- update diagnostics text using `SnapshotVersion`/`LogVersion` change detection
- update logs text only when `Logs` tab is active (or forced)

## Scope

- `host/WinUiHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`

## What Changed

### 1) WinUI dirty-flag + 10Hz UI refresh model

In `MainWindow.xaml.cs`:

- added `_uiRefreshTimer` at `100ms`
- added dirty flags:
  - `_pendingUiStateRefresh`
  - `_pendingRuntimeRefresh`
  - `_pendingAvatarRefresh`
  - `_pendingLogsRefresh`
- replaced immediate controller event refresh with deferred dirty-flag updates:
  - `Controller_StateChanged`
  - `Controller_DiagnosticsUpdated`
- introduced:
  - `MarkAllDirty(...)`
  - `ProcessPendingUpdates(force)`
  - `UpdateDiagnostics(force)`

### 2) Low-cost diagnostics text updates

- added snapshot/log version tracking:
  - `_lastRuntimeSnapshotVersion`
  - `_lastAvatarSnapshotVersion`
  - `_lastLogVersion`
- added text caches:
  - `_lastRuntimeText`
  - `_lastAvatarText`
  - `_lastLogsText`
- added helper builders:
  - `BuildRuntimeText(...)`
  - `BuildAvatarText(...)`
  - `BuildLogsText()`

Result:

- runtime/avatar/log text re-materialization is skipped unless data version or dirty state changed.

### 3) Log-tab-aware refresh behavior

In `MainWindow.xaml`:

- replaced stacked Runtime/Avatar/Logs panels with `TabView`:
  - `DiagnosticsTabControl`
  - tabs: `Runtime`, `Avatar`, `Logs`

In `MainWindow.xaml.cs`:

- added `DiagnosticsTabControl_SelectionChanged(...)`
- track active logs tab with `_isLogsTabActive`
- trigger log text rebuild only when:
  - force update, or
  - logs tab is active and log state changed

## Validation Results

Executed commands:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release
dotnet build host/WpfHost/WpfHost.csproj -c Release
dotnet build host/WinUiHost/WinUiHost.csproj -c Release
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1
```

Observed outcomes:

- `HostCore` build: PASS
- `WpfHost` build: PASS
- `WinUiHost` build: FAIL at XAML compile stage:
  - `MSB3073` (`XamlCompiler.exe`)
- `publish_hosts.ps1 -IncludeWinUi`:
  - WPF publish: PASS
  - WinUI preflight: PASS
  - WinUI publish: FAIL
  - failure class: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
  - managed diagnostics hint includes `WMC9999`
- `vsfavatar_quality_gate.ps1 -UseFixedSet`: `Overall=PASS`, `HostTrackStatus=PASS_WPF_BASELINE`
- `run_quality_baseline.ps1`: `Overall=PASS`

Evidence:

- `build/reports/host_publish_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/vsfavatar_gate_summary.txt`
- `build/reports/quality_baseline_summary.txt`

## WPF Parity Status

Parity model implementation status:

1. 10Hz UI refresh timer: DONE
2. Dirty-flag refresh path: DONE
3. Snapshot/Log version based update skipping: DONE
4. Logs update when Logs tab active: DONE
5. Render tick cadence unchanged: DONE

Validation closure status:

- compile/runtime closure remains BLOCKED by WinUI XAML toolchain path in this environment (`WMC9999` / `XamlCompiler.exe`)

## Remaining Work

1. Resolve WinUI XAML compile blocker (`WMC9999`) in current environment.
2. Run manual WinUI runtime smoke in an environment where WinUI app launch succeeds.
3. Capture runtime perf deltas (before/after) to complete parity evidence vs WPF.

## Detailed Change Inventory (This Round)

Code changes:

- `host/WinUiHost/MainWindow.xaml.cs`
  - introduced `_uiRefreshTimer` and 10Hz UI refresh loop
  - replaced event-driven full refresh with dirty-flag pipeline:
    - `MarkAllDirty`
    - `ProcessPendingUpdates`
    - `UpdateDiagnostics(force)`
  - added cached text builders:
    - `BuildRuntimeText`
    - `BuildAvatarText`
    - `BuildLogsText`
  - added logs-tab-aware refresh trigger via `DiagnosticsTabControl_SelectionChanged`

- `host/WinUiHost/MainWindow.xaml`
  - converted diagnostics pane to `TabView`
  - added named control `DiagnosticsTabControl`
  - split diagnostics UI into three tabs (`Runtime`, `Avatar`, `Logs`)

Documentation changes:

- `docs/reports/winui_ui_refresh_followup_ticket_2026-03-05.md`
  - added status update section for implementation round
- `docs/reports/wpf_ui_smoke_and_perf_2026-03-05.md`
  - refreshed latest verification timestamps
  - recorded latest launch-smoke failure (`exit=-532462766`)
- `docs/reports/wpf_verification_roundup_2026-03-05.md`
  - appended follow-up verification snapshot after parity implementation
- `docs/INDEX.md`
  - added this report entry
- `CHANGELOG.md`
  - added parity implementation and verification rerun entry

Generated report refreshes from verification runs:

- `build/reports/vrm_gate_fixed5.txt` (timestamp refresh)
- `build/reports/vrm_probe_fixed5.txt` (timestamp refresh)
