# Tracking Strict Runtime venv Runbook (2026-03-06)

## Goal

Make full tracking (`iFacial + Webcam MediaPipe + HybridAuto`) pass strict readiness by pinning a project-local Python runtime.

## Why

Recent failures were caused by WindowsApps python alias (`python.exe`/`py.exe`) not being a usable runtime.
Strict tracking contract now requires explicit Python for MediaPipe sanity.

## Standard

- Use project-local venv: `NativeAnimiq\.venv`
- Use pinned executable: `NativeAnimiq\.venv\Scripts\python.exe`
- Run strict readiness with `-MediapipePythonExe` explicitly set.

## Commands

From `NativeAnimiq` root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\setup_tracking_python_venv.ps1 -InstallPythonWithWinget
```

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\release_readiness_strict_tracking.ps1 -MediapipePythonExe .\.venv\Scripts\python.exe -SkipNativeBuild -NoRestore -SkipVersionContractCheck -SkipQualityBaseline
```

## PASS Criteria

- `build/reports/mediapipe_sidecar_sanity_summary.txt` -> `Overall: PASS`
- `build/reports/host_e2e_gate_summary.txt` -> `Overall: PASS`
- `build/reports/tracking_parser_fuzz_gate_summary.txt` -> `- Overall: PASS`
- `build/reports/release_gate_dashboard.txt`:
  - `Tracking HostE2E: ... PASS`
  - `Tracking Parser Fuzz: ... PASS`
  - `Tracking Mediapipe Sanity: ... PASS`
  - `TrackingContractCandidate: PASS`

## Failure Handling

- `python_source: missing`:
  - venv creation not completed or wrong path passed.
- `python_import_probe: FAIL`:
  - rerun venv setup and verify `mediapipe`/`opencv-python` installation.
- `Tracking Mediapipe Sanity: FAIL`:
  - inspect `mediapipe_sidecar_sanity_summary.txt` first, then rerun strict wrapper.
