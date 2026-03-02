# UI Render Benchmark Plan (2026-03-02)

## Scope

This document tracks the render-quality benchmark implementation against VSeeFace-like presentation goals.

- Targets: `host/WpfHost`, `host/WinUiHost`, `host/HostCore`, `src/nativecore`
- Priority: `P0 + P1`

## Implementation Summary (2026-03-03 update)

Render quality controls exposed from `nativecore` are now wired end-to-end through `HostCore` into both host UIs (WPF/WinUI), with runtime readback synchronization and debug overlay visibility control.

### HostCore

- `host/HostCore/HostController.cs`
  - Added internal apply/readback flow:
    - `ApplyRenderOptionsInternal(...)`
    - native set: `nc_set_render_quality_options`
    - native readback: `nc_get_render_quality_options`
  - Added camera mode mapping helpers:
    - `ToNativeCameraMode(...)`
    - `ToRenderCameraMode(...)`
  - Added background preset translation helpers:
    - `ApplyBackgroundPreset(...)`
    - `InferBackgroundPreset(...)`
  - Added centralized render state rebuild helper:
    - `BuildRenderUiState(...)`
  - Result: host-side state now reflects applied native options instead of relying only on optimistic UI-side state.

### WPF Host

- `host/WpfHost/MainWindow.xaml`
  - Added `Render` operation group:
    - `Broadcast Mode` toggle
    - `Framing` slider + value text
    - `Background` preset combo (`Dark Blue`, `Neutral Gray`, `Green Screen`)
    - `Show Debug Overlay` toggle
  - Added in-canvas debug overlay panel (`DebugOverlayPanel`, `DebugOverlayText`).

- `host/WpfHost/MainWindow.xaml.cs`
  - Added render control event handlers:
    - `BroadcastMode_Changed`
    - `FramingSlider_ValueChanged`
    - `BackgroundPreset_SelectionChanged`
    - `DebugOverlay_Changed`
  - Added reentry guard `_isSyncingRenderUi` to avoid event-loop churn during state sync.
  - Added UI<->state sync/apply methods:
    - `SyncRenderControlsFromState()`
    - `PushRenderUiState()`
  - Added diagnostics integration:
    - runtime diagnostics now include `RenderUi` line
    - debug overlay panel visibility/content follows `ShowDebugOverlay`.

### WinUI Host

- `host/WinUiHost/MainWindow.xaml`
  - Added same `Render` operation group and control surface as WPF.
  - Added in-canvas debug overlay panel for parity behavior.

- `host/WinUiHost/MainWindow.xaml.cs`
  - Added same event-driven apply path and reentry guard model as WPF:
    - render control handlers
    - `SyncRenderControlsFromState()`
    - `PushRenderUiState()`
  - Added diagnostics overlay update parity with WPF.

## Validation Snapshot

- `dotnet build host/HostCore/HostCore.csproj -c Release`: PASS.
- `dotnet build host/WpfHost/WpfHost.csproj -c Release`: blocked in current environment (`NU1301`, `api.nuget.org:443` unreachable).
- `dotnet build host/WinUiHost/WinUiHost.csproj -c Release`: blocked in current environment (`NU1301`, `api.nuget.org:443` unreachable).

Current verification scope confirms HostCore compile integrity and host UI wiring consistency by code inspection plus diagnostics path alignment. Full WPF/WinUI compile validation remains pending in a network-enabled environment.

## KPI Checklist

1. Framing
- Target: avatar occupies 65-85% of viewport height.
- Method: verify with runtime snapshot + visual inspection at 1080p.
- Status: In Progress.

2. Face Visibility
- Target: face occupies 25-40% of viewport height in bust mode.
- Method: bust-mode render capture and measurement.
- Status: In Progress.

3. Startup Pose Perception
- Target: no visible startup T-pose in default broadcast mode.
- Method: initialize -> load avatar sequence check.
- Status: In Progress.

4. Overlay Hygiene
- Target: default broadcast mode has no debug text overlay.
- Method: launch default settings and confirm overlay hidden.
- Status: Implemented.

5. Background Presets
- Target: three presets (`Dark Blue`, `Neutral Gray`, `Green Screen`).
- Method: switch presets and verify immediate background change.
- Status: Implemented.

6. DPI/Resize Stability
- Target: no blur/jitter after resize at 100/125/150% DPI.
- Method: resize stress test + runtime diagnostics.
- Status: In Progress.

7. Performance Regression
- Target: <=10% frame-time increase at 1080p.
- Method: compare `LastFrameMs` before/after render option changes.
- Status: Pending.

## Runtime Acceptance Scenarios

1. Initialize, load avatar, verify bust framing and centered composition.
2. Toggle debug overlay and verify on-screen diagnostics visibility changes.
3. Change background preset while rendering and verify immediate clear-color update.
4. Resize window with Spout active and verify render/output resolution remains aligned.

## Next Actions

1. Run WPF/WinUI release builds in CI or local network-enabled environment and attach results.
2. Execute KPI checks at 1080p for framing/face-occupancy measurements and record screenshots.
3. Add before/after `LastFrameMs` benchmark evidence to close performance regression item.
