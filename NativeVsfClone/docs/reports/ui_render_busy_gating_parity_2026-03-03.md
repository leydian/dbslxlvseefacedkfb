# UI Render/Preset Busy-Gating Parity Follow-up (2026-03-03)

## Summary

This follow-up hardens render and preset interactions so they obey the same busy-state policy already applied to lifecycle/output actions.

Scope:

- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

Primary outcomes:

- render interaction handlers now skip while HostCore reports busy operation state
- preset action handlers now skip while busy
- manual-only controls (`Yaw`, `FOV`) are enabled only in manual camera mode
- WPF and WinUI behavior is aligned using the same gate rules

## Detailed Changes

### 1) Shared render-interaction guard pattern in both hosts

Added helper:

- `ShouldSkipRenderInteraction() => _isSyncingRenderUi || _controller.OperationState.IsBusy`

Applied to handlers:

- `BroadcastMode_Changed`
- `CameraMode_SelectionChanged`
- `FramingSlider_ValueChanged`
- `HeadroomSlider_ValueChanged`
- `YawSlider_ValueChanged`
- `FovSlider_ValueChanged`
- `BackgroundPreset_SelectionChanged`
- `MirrorMode_Changed`
- `DebugOverlay_Changed`

Effect:

- prevents accidental render-state writes while any mutating host operation is in progress
- keeps render panel behavior consistent with existing action-level busy gating

### 2) Preset workflow busy gating

Added `IsBusy` short-circuit to:

- `SavePreset_Click`
- `ApplyPreset_Click`
- `DeletePreset_Click`
- `ResetRender_Click`

Effect:

- avoids preset persistence/apply race windows during concurrent operations
- keeps preset state transitions deterministic

### 3) Manual camera mode control gating

Updated UI enable rules:

- `YawSlider.IsEnabled = renderControlsEnabled && manualCameraMode`
- `FovSlider.IsEnabled = renderControlsEnabled && manualCameraMode`

Manual mode detection:

- `CameraModeComboBox.SelectedIndex == 2`
- fallback to `_controller.RenderState.CameraMode == RenderCameraMode.Manual`

Additionally, `CameraMode_SelectionChanged` now triggers `UpdateUiState()` before queueing apply to reflect enable-state changes immediately.

## Behavior Contract (WPF/WinUI parity)

Render panel interaction is allowed only when all are true:

- session initialized
- no host operation in progress (`!OperationState.IsBusy`)
- control-specific condition passes (e.g., manual mode for yaw/fov)

This contract is now implemented identically in WPF and WinUI code-behind.

## Verification

- `dotnet build host/HostCore/HostCore.csproj -c Release`
  - PASS

Notes:

- WPF/WinUI project build validation remains environment-dependent (NuGet/network) and was not re-run in this follow-up.

