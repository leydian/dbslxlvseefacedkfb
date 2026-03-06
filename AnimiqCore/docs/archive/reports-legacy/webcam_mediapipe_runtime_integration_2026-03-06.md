# Webcam MediaPipe Runtime Integration (2026-03-06)

## Summary

Tracking input was re-aligned from the temporary ONNX path to a MediaPipe-first runtime contract.

Primary outcomes:

1. `TrackingInputService` now starts a MediaPipe sidecar process and consumes per-frame JSON packets.
2. Webcam tracking state/diagnostics are driven by sidecar health (`initialized/receiving/stream-error/parse-failed`).
3. ARKit-like expression keys and head pose/position are mapped from MediaPipe packet fields into the existing native submit flow.
4. Release automation includes an optional MediaPipe sidecar sanity gate.

## Implementation

### HostCore runtime wiring

Updated:

- `host/HostCore/TrackingInputService.cs`

Key changes:

- Removed in-process pseudo webcam inference path.
- Added sidecar launch contract:
  - executable: `ANIMIQ_MEDIAPIPE_PYTHON` (default `python`)
  - script path: `ANIMIQ_MEDIAPIPE_SIDECAR_SCRIPT` (defaults to `tools/mediapipe_webcam_sidecar.py` lookup)
- Added frame packet parser (`System.Text.Json`) and live diagnostics update:
  - `capture_fps`
  - `inference_ms`
  - `LastErrorCode` with MediaPipe-specific codes
- Added robust teardown for sidecar process during tracking stop.

### MediaPipe sidecar

Added:

- `tools/mediapipe_webcam_sidecar.py`

Behavior:

- Opens the selected webcam.
- Runs MediaPipe face mesh per frame.
- Emits newline-delimited JSON packet with:
  - head yaw/pitch/roll
  - normalized head position
  - blink/mouth/smile values
  - capture/inference metrics
  - blendshape key/value subset

### Automation

Added:

- `tools/mediapipe_sidecar_sanity.ps1`

Updated:

- `tools/run_quality_baseline.ps1`
- `tools/release_readiness_gate.ps1`

New optional switch:

- `-EnableMediapipeSanity`

Artifact:

- `build/reports/mediapipe_sidecar_sanity_summary.txt`

## Notes

- WinUI build remains blocked by XAML toolchain issue in current environment and is tracked separately.
- MediaPipe sidecar requires local Python runtime with `mediapipe` and `opencv-python`.
