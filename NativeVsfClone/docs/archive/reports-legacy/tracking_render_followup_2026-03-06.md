# Tracking + Render Follow-up Summary (2026-03-06)

## Summary

This follow-up pass consolidates:

1. tracking precision upgrades (iFacial primary + webcam fallback orchestration),
2. runtime diagnostics expansion for hybrid tracking observability,
3. WPF render interaction improvements (direct drag/wheel camera control),
4. VRM node-transform application in loader path, and
5. native depth-state correction for visible render output stability.

## Detailed Changes

### 1) Hybrid tracking orchestration and precision tuning

Updated:

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/TrackingInputService.cs`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`
- `tools/mediapipe_webcam_sidecar.py`
- `tools/mediapipe_sidecar_sanity.ps1`

Key points:

- `TrackingDiagnostics` contract now includes:
  - `ActiveSource`
  - `FallbackCount`
  - `CalibrationState`
  - `ConfidenceSummary`
- Tracking runtime now supports source arbitration:
  - primary: iFacial (OSC)
  - fallback: webcam MediaPipe sidecar
  - hysteresis recovery back to iFacial
- Added adaptive calibration flow for expression channels:
  - warmup calibration phase
  - channel baseline tracking
  - normalized output scaling for stable expression range
- Sidecar packet parser now consumes optional `confidence` field and reflects source confidence in diagnostics.
- WPF/WinUI tracking status text now exposes hybrid runtime fields (active source, fallback count, calibration state, confidence).

### 2) WPF render host direct interaction (right-drag/wheel)

Updated:

- `host/WpfHost/RenderHwndHost.cs`
- `host/WpfHost/MainWindow.xaml.cs`

Key points:

- Added render-host mouse event surface:
  - right button drag start/move/end
  - mouse wheel delta
- WPF host now maps:
  - right drag => yaw adjustment
  - wheel => FOV adjustment
- Direct interactions force camera mode into manual path and immediately apply render UI state.

### 3) VRM node transform application in loader

Updated:

- `src/avatar/vrm_loader.cpp`

Key points:

- Added TRS/matrix decode helpers for node transform extraction.
- Applies node transform matrix to mesh vertex position stream when transform is non-identity.
- Adds warnings/diagnostics:
  - `VRM_MESH_MULTI_NODE_REF`
  - `VRM_NODE_TRANSFORM_APPLIED`

### 4) Native render depth-state correction

Updated:

- `src/nativecore/native_core.cpp`

Key point:

- Adjusted depth-stencil state selection in render path (`depth_read` -> `depth_write`) for the impacted draw section.

## Verification Snapshot

Executed:

```powershell
dotnet build host\HostCore\HostCore.csproj -c Release --no-restore
dotnet build host\WpfHost\WpfHost.csproj -c Release --no-restore
dotnet build host\WinUiHost\WinUiHost.csproj -c Release --no-restore
powershell -ExecutionPolicy Bypass -File .\tools\mediapipe_sidecar_sanity.ps1 -SkipImportProbe
```

Result:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL (existing WinUI XAML compiler/toolchain blocker)
- `mediapipe_sidecar_sanity`: FAIL in current environment (`python.exe` execution inaccessible); summary artifact emitted to `build/reports/mediapipe_sidecar_sanity_summary.txt`

## Notes

- This report reflects combined workspace follow-up changes that were in progress after the initial MediaPipe runtime pivot.
- WinUI build status remains tracked as known environment/toolchain issue and is not newly introduced by this pass.
