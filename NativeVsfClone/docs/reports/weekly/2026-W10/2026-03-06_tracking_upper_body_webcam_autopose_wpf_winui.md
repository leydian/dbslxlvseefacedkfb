# Tracking Upper-Body Webcam AutoPose + WPF/WinUI Surface Update (2026-03-06)

## Summary

Implemented upper-body auto-tracking v1 for the host tracking pipeline, scoped to shoulder + upper-arm pitch, with webcam-derived pose input and runtime merge against existing manual pose offsets.

This slice keeps the existing face/expression tracking path intact, adds an operator toggle for upper-body auto-tracking, and exposes upper-body diagnostics in both WPF and WinUI tracking status surfaces.

## Goals and Scope

- Add automatic upper-body movement on tracking session tick:
  - left/right shoulder pitch
  - left/right upper-arm pitch
- Keep compatibility with current manual pose workflow:
  - runtime behavior is `final_pose = manual_pose + auto_upper_body_pose`
- Default policy:
  - upper-body auto-tracking follows tracking session config and is enabled by default
  - operator can disable via UI toggle before tracking start
- Out of scope for v1:
  - lower-arm/hand/wrist/finger auto-tracking
  - OSC-native upper-body source integration

## Implementation Details

### 1) Host tracking contract and persistence extension

- Updated `host/HostCore/HostInterfaces.cs`:
  - added `UpperBodySmoothingProfile`
  - extended `TrackingStartOptions` with:
    - `UpperBodyEnabled`
    - `UpperBodyStrength`
    - `UpperBodySmoothing`
  - extended `TrackingDiagnostics` with:
    - `UpperBodyTrackingActive`
    - `UpperBodyConfidence`
    - `UpperBodyPacketAgeMs`
    - `UpperBodyStatus`
    - `UpperBodyLastError`
  - added `TrackingUpperBodyPose` runtime contract
  - added `ITrackingInputService.TryGetLatestUpperBodyPose(...)`

- Updated `host/HostCore/PlatformFeatures.cs`:
  - extended `TrackingInputSettings` with upper-body settings
  - default/legacy normalization now assigns:
    - `UpperBodyEnabled=true`
    - `UpperBodyStrength=1.0`
    - `UpperBodySmoothing=Balanced`

- Updated `host/HostCore/HostController.MvpFeatures.cs`:
  - `ConfigureTrackingInputSettings(...)` now accepts and persists upper-body settings
  - tracking config log line includes upper-body fields
  - `SetTrackingState(...)` preserves upper-body settings

### 2) Tracking runtime upper-body path

- Updated `host/HostCore/TrackingInputService.cs`:
  - added upper-body runtime state (raw/smoothed pitch values, confidence, packet age/status)
  - added upper-body smoothing profile tuning and stale-frame decay-to-neutral behavior
  - implemented `TryGetLatestUpperBodyPose(...)`
  - webcam packet path now updates upper-body pose independently from face source arbitration
  - diagnostics snapshots now publish upper-body status fields

- Parsing/payload path:
  - extended sidecar packet contract parsing to consume:
    - `left_shoulder_pitch_deg`
    - `right_shoulder_pitch_deg`
    - `left_upperarm_pitch_deg`
    - `right_upperarm_pitch_deg`
    - `upper_body_confidence`

### 3) Runtime pose submission merge

- Updated `host/HostCore/HostController.cs`:
  - tracking start options now pass upper-body config to `TrackingInputService.Start(...)`
  - tick path now reads latest upper-body pose and merges with manual pose offsets every frame
  - added merged runtime payload builder and submission cache:
    - avoids redundant `nc_set_pose_offsets(...)` when payload is unchanged
  - stop/reset paths clear auto-upper-body runtime state and re-apply manual-only offsets

### 4) Webcam sidecar extension

- Updated `tools/mediapipe_webcam_sidecar.py`:
  - added `mediapipe.pose` processing alongside existing `face_mesh`
  - derives v1 shoulder/upper-arm pitch estimates from shoulder-elbow landmark geometry
  - emits upper-body confidence from pose landmark visibility floor
  - includes new upper-body fields in every JSON frame payload (face detected / no-face fallback)

### 5) WPF/WinUI operator surfaces

- Updated `host/WpfHost/MainWindow.xaml(.cs)`:
  - added `TrackingUpperBodyEnabledCheckBox`
  - tracking start flow persists upper-body enabled flag
  - status text includes upper-body diagnostics fields
  - control enable policy gated with existing busy/tracking-active constraints
  - session defaults sync includes upper-body toggle state

- Updated `host/WinUiHost/MainWindow.xaml(.cs)`:
  - added `TrackingUpperBodyEnabledCheckBox`
  - tracking start flow persists upper-body enabled flag
  - status/runtime diagnostics text includes upper-body fields
  - session defaults sync includes upper-body toggle state

## Verification

- PASS: `dotnet build NativeVsfClone/host/HostCore/HostCore.csproj -c Debug`
- PASS: `dotnet build NativeVsfClone/host/WpfHost/WpfHost.csproj -c Debug`
- FAIL (known): `dotnet build NativeVsfClone/host/WinUiHost/WinUiHost.csproj -c Debug`
  - fails in WinUI XAML compiler stage (`XamlCompiler.exe` exit code 1)
  - tooling output did not emit concrete XAML location detail in this run

## Risk and Follow-up

- Current upper-body derivation is webcam-only heuristic v1 and intentionally conservative.
- WinUI build failure is tracked as follow-up for independent XAML compiler diagnosis in the existing toolchain environment.
