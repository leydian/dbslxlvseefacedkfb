# WPF UI v2: Navigation, Theme, Diagnostics Refinement (2026-03-06)

## Summary

Implemented a second-stage WPF UI refinement focused on visual polish and operator flow:

- Left rail is now interactive (single active section navigation).
- Diagnostics panel defaults to collapsed and expands on demand.
- Added live light/dark theme toggle with synchronized color tokens.
- Added low-intensity section transition animation and reduced unnecessary hidden-section sync work.

The change keeps existing host behavior contracts and event handlers intact.

## Scope

- Target: `host/WpfHost`
- Non-target: `host/WinUiHost`, `HostCore`, native runtime
- Compatibility: existing control wiring and runtime operation flow preserved

## Changed Files

- `host/WpfHost/App.xaml`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

## Detailed Changes

### 1) Token expansion and visual consistency

In `App.xaml`, introduced additional UI tokens used by v2 refinements:

- navigation rail/item brushes
- render shell brushes
- status bar brushes

This removes remaining hardcoded color islands and allows shared theme switching behavior.

### 2) Left rail upgraded to functional navigation

In `MainWindow.xaml` + `MainWindow.xaml.cs`:

- replaced static rail cards with clickable rail buttons
- introduced single-active-section model via `UiSection` enum
- wired handlers:
  - `NavGettingStarted_Click`
  - `NavSessionAvatar_Click`
  - `NavRender_Click`
  - `NavOutputs_Click`
  - `NavTracking_Click`
  - `NavPlatformOps_Click`
- added visual active/inactive state updates in `ApplyNavRailState()`

Result: the control workspace no longer behaves as one long scrolling list; only one section group is active at a time.

### 3) Diagnostics panel default-collapse policy

In `MainWindow.xaml.cs`:

- added pinned diagnostics state (`_diagnosticsPinnedVisible`)
- added rail control button (`ToggleDiagnosticsPanel_Click`)
- updated `ApplyModeVisibility()`:
  - diagnostics shown when `forced` (error flow) OR `pinned` (user toggle)
  - beginner mode no longer auto-opens diagnostics by default

Result: more render/control space by default with fast diagnostics access when needed.

### 4) Live light/dark theme toggle

In `MainWindow.xaml` + `MainWindow.xaml.cs`:

- added `ThemeToggleButton`
- implemented `ThemeToggle_Click()` and `ApplyThemeResources()`
- runtime resource color switching now updates:
  - surface/card/border/text
  - nav rail palette
  - render shell
  - status bar

Result: immediate theme switching without changing external contracts.

### 5) Interaction polish and lightweight optimization

In `MainWindow.xaml.cs`:

- added short section transition animation (`AnimateSectionTransition`, ~140ms)
- skipped several render/pose synchronization paths when corresponding section is hidden

Result: smoother perceived transitions and reduced unnecessary UI update work in non-visible sections.

## Verification

- Build command:
  - `dotnet build NativeAnimiq\host\WpfHost\WpfHost.csproj -c Release --no-restore`
- Result:
  - PASS (`0 warnings`, `0 errors`)

