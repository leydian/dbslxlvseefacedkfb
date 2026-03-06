# WPF UI Flow + Refresh Throttle Update (2026-03-05)

## Summary

Implemented the WPF-first UI/performance plan to reduce UI thread churn while preserving the 60Hz render loop.

Core direction:

- keep render tick at ~16ms
- throttle UI diagnostics/status refresh to 10Hz
- render full logs text only when the Logs tab is active
- re-layout WPF controls into an explicit operation flow

## Scope

- `host/HostCore/HostUiState.cs`
- `host/HostCore/HostController.cs`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

## What Changed

### 1) HostCore snapshot/log version signals

- `DiagnosticsSnapshot` now includes:
  - `SnapshotVersion`
  - `LogVersion`
- `HostController` increments:
  - `_snapshotVersion` on each diagnostics publish
  - `_logVersion` on each log enqueue

Result:

- UI can cheaply detect whether runtime/avatar/log sections actually changed before rebuilding heavy text blocks.

### 2) WPF layout restructured for operation flow

- Added top `Quick Actions` section:
  - `Initialize`, `Load`, `Start Spout`, `Start OSC`, `Shutdown`
  - compact quick status line
- Left panel groups are ordered as:
  - `1) Session` -> `2) Avatar` -> `3) Render` -> `4) Outputs`
- Render group split into:
  - `Basic` block (broadcast/camera/framing)
  - `Advanced` expander (headroom/yaw/fov/background/debug/presets)
- Diagnostics tab control now has explicit selection event wiring for log-tab-aware refresh behavior.

### 3) WPF refresh model changed to 60Hz render + 10Hz UI

- Added `_uiRefreshTimer` at `100ms`.
- `StateChanged` / `DiagnosticsUpdated` no longer trigger immediate full refresh.
- Introduced dirty flags for:
  - UI state
  - runtime diagnostics
  - avatar diagnostics
  - logs
- `UpdateDiagnostics(...)` now:
  - uses `SnapshotVersion`/`LogVersion` to skip redundant text rebuilds
  - updates logs text only when `Logs` tab is active (or forced refresh)
  - keeps overlay/runtime/avatar text cached and only reassigns when changed

## Validation

Executed on 2026-03-05 (local workspace):

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release
dotnet build host/WpfHost/WpfHost.csproj -c Release
```

Results:

- `HostCore`: PASS
- `WpfHost`: PASS (after allowing network restore in this environment)

## Operational Note

WinUI parity for this exact refresh-throttle/layout pattern remains a follow-up item by policy; this round intentionally prioritizes the WPF release path.
