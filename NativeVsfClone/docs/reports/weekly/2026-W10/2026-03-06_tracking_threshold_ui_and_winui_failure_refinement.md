# Tracking Threshold UI + WinUI Failure Refinement (2026-03-06)

## Summary

This pass implemented the execution-plan items around tracking threshold operability and host-visible native submit failures, while tightening WinUI minimal repro failure classification output.

Primary outcomes:

- tracking parse/drop warn thresholds are now operator-configurable in both WPF and WinUI host UIs.
- native tracking/expression submit failures are preserved through the tick path and surfaced consistently in host status text.
- WinUI minimal repro summary now emits richer classification hints to reduce blocker triage ambiguity.

## Implemented Changes

### 1) HostCore tick-path error preservation

Updated:

- `host/HostCore/HostController.cs`

Change:

- Introduced a native-submit error carry path (`nativeSubmitErrorCode`) during `Tick(...)`.
- Prevented `TrackingInputService.GetDiagnostics()` from overwriting same-tick native submit failures.
- Unified native error handling for:
  - `NC_SET_TRACKING_FRAME_*`
  - `NC_SET_EXPRESSION_WEIGHTS_*`

Result:

- host UI no longer intermittently loses native submit error code visibility under high-frequency diagnostics refresh.

### 2) WPF tracking threshold UX completion

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

Change:

- Added tracking controls:
  - `Parse Warn`
  - `Drop Warn`
- Added parse/normalize logic on start action (`1..10000` clamped).
- Wired thresholds into `ConfigureTrackingInputSettings(...)`.
- Included configured thresholds in runtime status text:
  - `parse_warn=...`
  - `drop_warn=...`
- Added host-side `BuildTrackingErrorHint(...)` mapping for actionable status hints from major tracking/native error codes.

### 3) WinUI tracking threshold UX completion

Updated:

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Change:

- Added same threshold controls and clamp behavior as WPF.
- Persisted and restored threshold values from session state.
- Wired thresholds into `ConfigureTrackingInputSettings(...)`.
- Extended tracking status text with threshold and hint fields.

### 4) WinUI repro failure classification refinement

Updated:

- `tools/winui_xaml_min_repro.ps1`

Change:

- Expanded classification branches and added summary-level `FailureHints`.
- Added concrete classification signals for:
  - `TOOLCHAIN_WINDOWS_SDK_INCOMPLETE`
  - `WINDOWSAPPSDK_RESTORE_INCOMPLETE`
  - existing `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`, `NUGET_SOURCE_UNREACHABLE`, `NU1101`, `XAML_COMPILER_EXEC_FAIL`.

Result:

- minimal repro output is more actionable without opening full diag logs.

### 5) Release execution board status sync

Updated:

- `docs/reports/weekly/2026-W10/2026-03-06_release_execution_board_20.md`

Change:

- Item 10 (`Tracking threshold config externalization`) moved to `DONE`.
- Item 11 (`Tracking/expression native submit failures surfaced in UI`) moved to `DONE`.
- Item 15 remained `IN_PROGRESS` with narrowed remaining scope wording.

## Verification

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release --no-restore
dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore
dotnet build host/WinUiHost/WinUiHost.csproj -c Release --no-restore
powershell -ExecutionPolicy Bypass -File tools/winui_xaml_min_repro.ps1 -NoRestore
```

Observed:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL in current environment (`NU1301`, `api.nuget.org:443` access blocked)
- `winui_xaml_min_repro`: summary emitted with:
  - `FailureClass=WINDOWSAPPSDK_RESTORE_INCOMPLETE`
  - `FailureHints=WindowsAppSDK package resolution issue detected.`

## Notes

- WinUI final blocker closure still depends on environment/network/toolchain readiness.
- This pass completed host-side operability and diagnostics visibility work for tracking threshold and native submit error surface.
