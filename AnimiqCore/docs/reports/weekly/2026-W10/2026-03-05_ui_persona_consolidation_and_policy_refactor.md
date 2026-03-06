# UI Persona Consolidation and Policy Refactor Report (2026-03-05)

## Summary

This update applies the agreed UI plan across WPF/WinUI host shells with three goals:

- remove operator confusion from duplicated actions (user persona)
- centralize UI state rules to reduce parity drift (developer persona)
- improve execution-path clarity and status readability (planner persona)

Scope was intentionally limited to host UI behavior and presentation. Native runtime APIs were unchanged.

## Implemented Changes

### 1) Shared HostCore UI policy extraction

Added:

- `host/HostCore/HostUiPolicy.cs`

Details:

- Introduced `HostUiAvailability` to compute action/control enablement in one place.
- Introduced `HostUiStatusText` to standardize status-line text generation.
- Added:
  - `HostUiPolicy.EvaluateAvailability(...)`
  - `HostUiPolicy.BuildStatusText(...)`
- Inputs include `HostSessionState`, `OutputState`, `HostOperationState`, `HostValidationState`, `RenderUiState`, and current manual camera selection.

Result:

- WPF/WinUI now share the same decision logic for core operation gating, reducing copy/paste divergence risk.

### 2) WPF flow simplification and single action source

Updated:

- `host/WpfHost/MainWindow.xaml`

Details:

- `Quick Actions` section was changed from interactive button set to informational guidance + status summary only.
- Duplicate action buttons were removed from `Quick Actions`.
- Canonical named buttons are now the section-level controls:
  - Session: `InitializeButton`, `ShutdownButton`
  - Avatar: `LoadButton`
  - Outputs: `StartSpoutButton`, `StartOscButton`

Result:

- Eliminated duplicate command entry points that could show inconsistent enabled/disabled behavior.
- Preserved the intended operational path (`Initialize -> Load -> Start outputs`) with clearer hierarchy.

### 3) WinUI parity + readability + minimum size guard

Updated:

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Details:

- `UpdateUiState()` now consumes shared HostCore policy methods instead of local duplicated rule logic.
- Root grid now specifies minimum layout size (`MinWidth=1240`, `MinHeight=760`).
- Status strip text foreground colors were explicitly set for dark background readability:
  - normal status text: `#D7E6F5`
  - error text: `#F2B8B5`
- Added AppWindow-based minimum size enforcement in code-behind:
  - startup normalization in `ConfigureWindowBounds()`
  - resize clamp in `AppWindow_Changed(...)`
  - event unsubscription in `MainWindow_Closed(...)`

Result:

- WinUI state behavior aligns with WPF policy source.
- Status strip contrast is stable across theme defaults.
- Narrow-window layout collapse risk is reduced.

## Validation

Executed:

```powershell
dotnet build NativeAnimiq\host\HostCore\HostCore.csproj -c Release
dotnet build NativeAnimiq\host\WpfHost\WpfHost.csproj -c Release
dotnet build NativeAnimiq\host\WinUiHost\WinUiHost.csproj -c Release -p:Platform=x64
```

Outcome:

- `HostCore`: PASS (`0 warnings`, `0 errors`)
- `WpfHost`: PASS (`0 warnings`, `0 errors`)
- `WinUiHost`: FAIL at existing XAML compiler stage (`XamlCompiler.exe` exit code 1)

Notes:

- WinUI failure class remains consistent with the previously tracked environment/toolchain blocker path.
- No additional runtime/nativecore behavior regressions were introduced by this UI-only change set.

## Files Changed

- `host/HostCore/HostUiPolicy.cs` (new)
- `host/WpfHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`
