# Tracking WPF Benchmark Controls: Basic + Advanced Layer (2026-03-06)

## Summary

This pass implemented the planned benchmark-style tracking settings flow in the WPF host by introducing a beginner-first control layer and preserving full advanced controls behind an expander.

Primary outcomes:

- the tracking panel now exposes high-impact stabilization controls first (`stability`, `loss timeout`, `show tracking position`).
- existing detailed controls remain available under `Advanced` without removing prior capabilities.
- new basic controls are fully wired to existing persisted tracking/render settings to avoid introducing a parallel config system.
- a one-click tracking defaults reset path was added for quick recovery.

## Implemented Changes

### 1) Tracking panel UX restructuring (WPF)

Updated:

- `host/WpfHost/MainWindow.xaml`

Change:

- Replaced the single long tracking form with:
  - **Basic stabilization card**:
    - `TrackingStabilitySlider` (`0..100`)
    - `TrackingStaleTimeoutSlider` (`100..2000ms`)
    - `TrackingShowPositionCheckBox` (debug overlay bridge)
  - **Advanced expander**:
    - existing source/lock/profile/filter/deadband/upper-body and threshold controls retained.
- Added `TrackingResetDefaultsButton` to restore a stable default configuration.

Result:

- beginner users see fewer, higher-value controls first.
- power users still have full parameter access in the same panel.

### 2) Basic controls wiring to existing runtime settings

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Change:

- Added handlers:
  - `TrackingStabilitySlider_ValueChanged`
  - `TrackingStaleTimeoutSlider_ValueChanged`
  - `TrackingShowPosition_Changed`
  - `TrackingResetDefaults_Click`
- Stability slider mapping:
  - derives `PoseFilterProfile` (`Reactive/Balanced/Stable`)
  - derives `TrackingLatencyProfile` (`LowLatency/Balanced/Stable`)
  - computes `PoseDeadbandDeg` from slider value.
- Loss-timeout slider updates `TrackingInputSettings.StaleTimeoutMs`.
- Show-position checkbox bridges to render `DebugOverlay` and uses existing render apply queue.
- Reset defaults applies a coherent baseline through `ConfigureTrackingInputSettings(...)`:
  - `HybridAuto`, `Stable`, `deadband=0.9`, `stale_timeout=500ms`, `fps=30`, warnings reset, auto-stability on, upper-body on.

Result:

- all new UX controls mutate real runtime/persisted state via existing HostCore APIs.
- no duplicate configuration state introduced.

### 3) UI sync and state consistency integration

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Change:

- Added sync guard/flow:
  - `_isSyncingTrackingBasicUi`
  - `SyncTrackingBasicControlsFromState()`
- Integrated sync with:
  - startup/default session application
  - tracking pose filter sync updates
  - render state sync (`DebugOverlay` -> tracking position checkbox)
  - enabled/disabled state gating when busy or tracking active.
- `StartTracking_Click` now uses slider-selected stale timeout value when present.

Result:

- basic/advanced controls stay coherent during mode switches, startup restore, and runtime refresh.
- control feedback avoids recursive update loops.

## Verification

Executed:

```powershell
dotnet build host/WpfHost/WpfHost.csproj -c Release
```

Observed:

- initial sandbox build failed due to blocked `api.nuget.org:443` restore.
- escalated build completed successfully.
- final result: `WpfHost` and `HostCore` build PASS (`0 errors`, `0 warnings`).

## Notes

- This change is WPF-only in this pass.
- WinUI parity for the same basic/advanced tracking pattern can be implemented as a follow-up.
