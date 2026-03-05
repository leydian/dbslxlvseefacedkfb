# WPF Arm Pose Slider Wiring (2026-03-06)

## Summary

Completed WPF-side wiring for arm pose control so T-pose avatar adjustment can be driven with a slider-based UX in two modes:

- both-arm synchronized pitch control
- per-arm independent pitch control

This pass focuses on host interaction flow and state sync. Native pose offset API/contract remains unchanged.

## Implemented Changes

### 1) Arm slider event pipeline

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Behavior:

- Added `ArmPoseSlider_ValueChanged(...)` handler.
- Keeps value labels synchronized for:
  - `ArmBothPitch`
  - `ArmLeftPitch`
  - `ArmRightPitch`
- Applies busy-state and UI-sync guard before mutation:
  - skips apply when `_controller.OperationState.IsBusy`
  - skips re-entrant writes while `_isSyncingPoseUi` is active
- `Both` slider write path:
  - pushes the same pitch to left/right arm sliders
  - applies both offsets via `SetPoseOffset(...)`
- per-arm slider write path:
  - applies only targeted side
  - recomputes and updates `Both` slider as average of left/right

### 2) Pose offset application helper

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Behavior:

- Added `ApplyArmPitchOffset(PoseBoneKind bone, float pitchDeg)`.
- Preserves existing Yaw/Roll for the target bone while only replacing Pitch.
- Uses existing `HostController.SetPoseOffset(...)` path (no new interop surface).

### 3) UI availability and state synchronization

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Behavior:

- Integrated arm sliders into render-control availability policy:
  - enabled only when `uiState.RenderControlsEnabled == true`
- Extended `SyncPoseControlsFromState()`:
  - pulls `LeftUpperArm` / `RightUpperArm` pitch from current controller state
  - updates both per-arm slider values and combined `Both` average value
- Extended `ApplySelectedPoseOffset()`:
  - when selected pose bone is left/right upper arm, triggers sync to keep arm sliders and legacy bone selector controls consistent.

## Verification Summary

### Build / compile checks

- `cmake --build NativeVsfClone/build --config Release --target nativecore`
  - result: pass
- `dotnet build NativeVsfClone/host/WpfHost/WpfHost.csproj -c Release`
  - result: pass
  - warnings: 0
  - errors: 0

### Behavioral sanity checks (source-level)

- Confirmed `Both` slider writes left/right pitch in one interaction path.
- Confirmed per-arm updates do not overwrite the opposite arm.
- Confirmed arm controls follow existing busy gating rules.
- Confirmed no native ABI/header contract changes in this pass.

## Notes

- Scope is WPF host only.
- WinUI parity is intentionally out of scope for this change set.
