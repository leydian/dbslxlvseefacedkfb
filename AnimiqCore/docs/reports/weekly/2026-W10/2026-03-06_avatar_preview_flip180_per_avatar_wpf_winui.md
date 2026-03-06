# Avatar Preview Flip180 Per-Avatar Persistence (WPF + WinUI) (2026-03-06)

## Summary

Implemented an operator-facing fix for back-facing avatars by adding a per-avatar preview orientation toggle and persistence contract.

Primary outcomes:

- operators can flip current avatar preview/front direction by 180 degrees from both hosts,
- toggle state is persisted per avatar path and auto-reapplied on next load,
- default behavior remains runtime auto-decision unless the operator sets an override.

## Problem Context

- runtime-level preview yaw can legitimately resolve to a back-facing initial direction for some assets,
- global default policy changes were too broad and risked regressions across other avatar families,
- operators needed an explicit per-avatar control that survives restart/reload and works identically in WPF/WinUI.

## Implementation Details

### A) HostCore persistence model extension

Updated:

- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.MvpFeatures.cs`

Changes:

- `RecentAvatarEntry` extended with:
  - `PreviewFlip180` (`bool`, default `false`)
- `SessionPersistenceModel` schema version bumped:
  - `10 -> 11`
- normalization/migration path updates:
  - missing field safely defaults to `false`
  - legacy payloads remain loadable without breaking recent-avatar order/state
- added controller helpers:
  - `GetAvatarPreviewFlip180(path)`
  - `SetAvatarPreviewFlip180Preference(path, enabled)`

### B) Load-time apply path + runtime toggle action

Updated:

- `host/HostCore/HostController.cs`

Changes:

- on successful `LoadAvatar`, host now applies stored per-avatar flip if enabled:
  - `ApplyStoredAvatarPreviewFlipIfNeeded(path)`
- added explicit UI-driven action:
  - `ToggleAvatarPreviewFlip180(path)`
- yaw normalization helper added for safe signed range:
  - `NormalizeSignedDegrees(...)` (`[-180, 180]`)

Behavior:

- when toggle is enabled for the selected avatar:
  - load path applies `+180` yaw once after render options reapply
- when toggle is disabled:
  - existing runtime/front policy behavior is untouched

### C) WPF/WinUI UI parity

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- avatar panel now exposes:
  - `앞/뒤 전환 (이 아바타 저장)` button
  - current persisted state text (`저장: 기본` / `저장: 반전(180)`)
- UI state sync integrated with:
  - avatar path input changes
  - recent-avatar selection changes
  - load completion and render-state sync
- busy/load gating aligned with existing operation guards.

## Verification

Executed:

- static reference checks (`rg`) for:
  - new API wiring,
  - WPF/WinUI event hookup parity,
  - persistence schema field usage.

Observed:

- all planned code paths are present and connected across HostCore + both hosts.

Build verification status:

- blocked in this environment due restore/source prerequisites:
  - missing local source: `D:\dbslxlvseefacedkfb\NativeVsfClone\build\nuget-mirror`
  - network-restricted `nuget.org` access
  - unresolved package restore (`OpenCvSharp4`, `OpenCvSharp4.runtime.win`, `Microsoft.Windows.SDK.NET.Ref`)

## Scope Notes

- no nativecore API/schema changes,
- no global VRM/MIQ preview yaw baseline policy change,
- override keying remains absolute avatar path (as selected by host).

