# WPF UI v3: Shortcuts, Focus Routing, Workspace Persistence (2026-03-06)

## Summary

Implemented third-stage WPF UI refinement focused on operational efficiency and continuity:

- Added keyboard shortcut layer for core navigation/actions.
- Added keyboard-accessible left-rail traversal and focus ring polish.
- Added section-aware primary focus routing after navigation.
- Persisted/restored workspace state (active section, theme, diagnostics pin) via existing Host session store.

This update extends v2 without changing runtime-facing host contracts.

## Scope

- Target: `host/WpfHost`, `host/HostCore` session persistence surface
- Non-target: `host/WinUiHost`, native runtime behavior
- Compatibility: existing UI actions and event handlers preserved

## Changed Files

- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.MvpFeatures.cs`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

## Detailed Changes

### 1) Workspace persistence model upgrade

`SessionPersistenceModel` was expanded (version `8`) with:

- `UiActiveSection`
- `UiThemeMode`
- `UiDiagnosticsPinned`

Normalization and legacy fallback paths were updated to guarantee safe defaults (`getting_started`, `light`, `false`).

### 2) HostController persistence API extension

Added UI workspace accessors in `HostController.MvpFeatures.cs`:

- `GetUiWorkspaceState()`
- `SetUiWorkspaceState(activeSection, themeMode, diagnosticsPinned)`

Values are normalized and persisted through existing `PersistSessionSnapshot()` flow.

### 3) Shortcut and keyboard navigation layer

In `MainWindow.xaml.cs`:

- global shortcuts:
  - `Ctrl+1..6`: section switch
  - `Ctrl+D`: diagnostics panel toggle
  - `Ctrl+T`: theme toggle
- nav keyboard flow:
  - `Up/Down`: rail button traversal
  - `Enter/Space`: invoke focused rail action

In `MainWindow.xaml`:

- rail/tool buttons now expose shortcut tooltips.
- nav button style adds keyboard-focus border accent.

### 4) Focus routing for faster operator flow

Added section-primary focus routing after section activation and startup restore:

- Getting Started -> `PrimaryActionButton`
- Session/Avatar -> `AvatarPathTextBox`
- Render -> `FramingSlider`
- Outputs -> `SpoutChannelTextBox`
- Tracking -> `TrackingPortTextBox`
- Platform Ops -> `RunPreflightButton`

### 5) Restore behavior on startup

`ApplySessionDefaultsToUi()` now restores:

- last active section
- last theme mode
- diagnostics pin state

then applies mode visibility and target focus.

## Verification

- `dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore`
- PASS (`0 warnings`, `0 errors`)

