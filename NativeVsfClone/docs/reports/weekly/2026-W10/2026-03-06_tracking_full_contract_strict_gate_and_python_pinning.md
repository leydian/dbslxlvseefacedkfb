# Tracking Full-Contract Strict Gate and MediaPipe Python Pinning (2026-03-06)

## Summary

This slice closes the "tracking partially green" gap by enforcing a strict, release-level tracking contract:

- MediaPipe runtime check must use an explicit Python executable.
- Tracking HostE2E, Tracking Fuzz, and MediaPipe sanity are all required for release readiness.
- Release dashboard now exposes tracking-contract status as first-class rows and uses it in candidate decisions.

## Problem

Prior status could look healthy for host publish/baseline flows while webcam tracking remained operationally uncertain:

- `host_e2e`: PASS
- `tracking_parser_fuzz`: PASS
- `mediapipe_sidecar_sanity`: FAIL (python executable inaccessible/missing)
- release readiness runs often had MediaPipe sanity disabled

Result: full-path tracking certainty (iFacial + Webcam + HybridAuto) was not guaranteed.

## Implementation

### 1) MediaPipe sanity: explicit Python pin contract

File:
- `tools/mediapipe_sidecar_sanity.ps1`

Changes:
- Added Python resolution order:
  1. `-PythonExe`
  2. `VSFCLONE_MEDIAPIPE_PYTHON`
  3. fallback `python` (only when explicit pin is not required)
- Added strict switch:
  - `-RequireExplicitPythonExe`
- Added richer summary fields:
  - `PythonExe`
  - `PythonSource` (`cli|env:VSFCLONE_MEDIAPIPE_PYTHON|default|missing`)
  - `RequireExplicitPythonExe`
- Added deterministic fail message when explicit pin is required but missing.

### 2) Release readiness: strict tracking contract default

File:
- `tools/release_readiness_gate.ps1`

Changes:
- Added options:
  - `-EnableTrackingFuzz`
  - `-DisableStrictTrackingContract`
  - `-MediapipePythonExe`
- Default strict mode now effectively requires:
  - `MediaPipe sidecar sanity` (with `-RequireExplicitPythonExe`)
  - `Host E2E gate`
  - `Tracking parser fuzz gate`
- Added summary metadata:
  - `StrictTrackingContract`
  - `EffectiveEnableHostE2E`
  - `EffectiveEnableTrackingFuzz`
  - `EffectiveEnableMediapipeSanity`
  - `MediapipePythonExe`

### 3) Release dashboard: tracking contract in candidate logic

File:
- `tools/release_gate_dashboard.ps1`

Changes:
- Added rows:
  - `Tracking HostE2E`
  - `Tracking Parser Fuzz`
  - `Tracking Mediapipe Sanity`
- Added `tracking_contract_all_pass` aggregation in JSON summary.
- `ReleaseCandidateWpfOnly` and `ReleaseCandidateFull` now require tracking contract all-pass.

### 4) Repro guidance

File:
- `host/HostCore/HostController.MvpFeatures.cs`

Changes:
- Added strict readiness repro command example with explicit:
  - `VSFCLONE_MEDIAPIPE_PYTHON`

## Verification

Executed checks:

1. Script command availability:
- `mediapipe_sidecar_sanity.ps1`: OK
- `release_readiness_gate.ps1`: OK
- `release_gate_dashboard.ps1`: OK

2. Dashboard run:
- `powershell -ExecutionPolicy Bypass -File .\tools\release_gate_dashboard.ps1`: PASS
- Output includes tracking rows and tracking-driven candidate decisions.

3. Readiness behavior:
- Relaxed run (`-DisableStrictTrackingContract`): PASS
- Strict default run: FAIL (expected without explicit Python pin)
  - `python_source: missing`
  - `python_executable: FAIL (missing explicit python executable; set VSFCLONE_MEDIAPIPE_PYTHON or pass -PythonExe)`

## Operational outcome

- Release readiness can no longer report green for full tracking without a configured MediaPipe runtime.
- Tracking certainty now aligns with gate decisions and dashboard visibility.

