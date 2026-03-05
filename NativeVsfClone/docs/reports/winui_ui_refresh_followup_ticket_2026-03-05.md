# WinUI Follow-up Ticket: UI Refresh Throttle Parity (2026-03-05)

## Goal

Mirror the WPF-side refresh-throttle optimizations into WinUI host while keeping current WPF-first release policy unchanged.

## In Scope

- `host/WinUiHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml` (only if needed for tab-selection hooks/status parity)
- optional shared helper extraction if duplication is excessive

## Out of Scope

- WinUI XAML toolchain unblock (`WMC9999`) root-cause remediation
- CI policy switch back to WinUI-required gate

## Required Changes

1. Introduce 10Hz UI refresh timer in WinUI host path.
2. Replace immediate full-refresh event handlers with dirty-flag refresh.
3. Use `DiagnosticsSnapshot.SnapshotVersion` and `LogVersion` for low-cost change detection.
4. Render logs text only when Logs tab is active (or forced refresh).
5. Keep render tick path cadence unchanged.

## Validation Checklist

1. Build:
   - `dotnet build host/WinUiHost/WinUiHost.csproj -c Release`
2. Runtime smoke:
   - initialize/load/render/outputs/resize/shutdown
3. Diagnostics behavior:
   - runtime/avatar text update only when snapshot version changes
   - logs update only when log version changes and logs tab active

## Risks

- WinUI environment/toolchain instability can block runtime validation independent of UI-refresh logic.
- Feature drift may occur if WPF and WinUI continue separate code-behind evolution without shared abstraction.

## Definition of Done

1. WinUI host compiles with parity refresh model.
2. Manual runtime smoke passes in an environment where WinUI can run.
3. New report documents behavior parity and any remaining deltas vs WPF.

## Status Update (2026-03-05, parity implementation round)

Implementation status:

- required changes 1-5: DONE in code
  - `host/WinUiHost/MainWindow.xaml.cs`
  - `host/WinUiHost/MainWindow.xaml`

Validation status:

- WinUI compile closure: BLOCKED
  - current blocker: `WMC9999` / `XamlCompiler.exe` (`MSB3073`)
  - classification: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
- manual WinUI runtime smoke: NOT EXECUTED (toolchain blocker)

Reference report:

- `docs/reports/winui_ui_refresh_throttle_parity_2026-03-05.md`
