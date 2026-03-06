# WPF Render-Only Mode Toggle (2026-03-06)

## Summary

This change adds a render-only presentation mode to the WPF host so operators can show only the avatar render surface without control panels.

Implemented scope:

- WPF host only (`host/WpfHost`)
- same-window toggle (no extra preview window)
- render background behavior unchanged (existing `BackgroundPreset` remains active)
- entry/exit via UI button and `F11`
- render-only state is session-local (not persisted across restart)

## Implemented Changes

### 1) Mode entry/exit controls

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

Behavior:

- Added `RenderOnlyToggleButton` in the Mode section.
- Added `RenderOnlyToggle_Click` handler.
- Added `PreviewKeyDown` handler on window; `F11` toggles render-only mode.
- Button caption switches dynamically:
  - `렌더 전용 모드 (F11)`
  - `일반 UI 복귀 (F11)`
- Toggle button is disabled while host operation is busy to avoid layout transitions during critical operations.

### 2) Render-only layout switching

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

XAML structure changes:

- Named layout elements for runtime visibility/size control:
  - `ControlsColumn`
  - `SplitterColumn`
  - `ControlPanelScrollViewer`
  - `MainGridSplitter`
  - `StatusBarBorder`

Runtime behavior (`ApplyModeVisibility`):

- When render-only is enabled:
  - left controls column width => `0`
  - splitter column width => `0`
  - left control panel => hidden
  - splitter => hidden
  - diagnostics row height => `0`
  - diagnostics tab => hidden
  - bottom status bar => hidden
- When render-only is disabled:
  - original 3-column layout restored (`440 / 12 / *`)
  - diagnostics and status bar visibility returns to existing mode policy (beginner/advanced + diagnostics forced visible logic)

This keeps previous UI-mode semantics intact while adding a higher-priority render-only presentation state.

### 3) Temporary on-screen exit hint

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

Behavior:

- Added `RenderOnlyHintOverlay` over render area.
- On entering render-only mode, overlay is shown with:
  - `렌더 전용 모드 (Render-only) · F11로 UI 복귀`
- Hint auto-hides after 3 seconds (`RenderOnlyHintDuration`).
- Hint remains hidden outside render-only mode.

### 4) Render target sync after layout transition

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Behavior:

- Added `RefreshRenderTargetAfterLayoutChange()` and invoked it after mode toggles.
- On dispatcher background priority:
  - refresh logical/pixel render metrics
  - if session/window is attached, call resize on native render target

This prevents stale render-size state after major host layout changes.

## Verification Summary

### Build / compile checks

- `dotnet build host/WpfHost/WpfHost.csproj -nologo`
  - result: pass
  - warnings: 0
  - errors: 0

### Source-level checks

- Confirmed added XAML controls are connected to code-behind handlers.
- Confirmed `F11` is handled at window level (`PreviewKeyDown`).
- Confirmed render-only branch returns before beginner/advanced mode visibility logic.
- Confirmed no `HostCore`/native API contract changes were introduced.

## Known Risks or Limitations

- WPF-only implementation in this pass; WinUI host parity is not included.
- Render-only mode does not alter window chrome/state (not borderless/fullscreen), only internal layout visibility.
- Mode is intentionally not persisted, so app always starts in normal UI view.

## Next Steps

1. If broadcast workflow needs stronger kiosk behavior, add optional borderless/fullscreen policy for render-only mode.
2. If cross-host parity is required, implement equivalent toggle and overlay in `host/WinUiHost`.
3. Add automated UI smoke script coverage for `F11` toggle + render resize regression.
