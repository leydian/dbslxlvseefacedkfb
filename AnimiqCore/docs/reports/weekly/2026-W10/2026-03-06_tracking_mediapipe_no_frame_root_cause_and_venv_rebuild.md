# Tracking MediaPipe NO_FRAME Root Cause + Venv Rebuild (2026-03-06)

## Summary

Resolved a follow-up tracking startup failure after previous Python pinning:

- popup code: `TRACKING_MEDIAPIPE_NO_FRAME`
- status: `mediapipe sidecar started but no frame received`

This was not a camera permission-only issue. Root cause was runtime API mismatch
between the sidecar implementation and installed `mediapipe` package shape.

## Failure Progression

1. Initial issue (`TRACKING_MEDIAPIPE_START_FAILED`, sidecar exit `code=2`) was
   mitigated by pinning `ANIMIQ_MEDIAPIPE_PYTHON` to project `.venv`.
2. New issue surfaced as `TRACKING_MEDIAPIPE_NO_FRAME`.
3. Direct sidecar execution exposed pre-frame exception:

```text
AttributeError: module 'mediapipe' has no attribute 'solutions'
```

The sidecar (`tools/mediapipe_webcam_sidecar.py`) uses `mp.solutions.face_mesh`
and `mp.solutions.pose`; with missing `solutions`, capture loop never emits frame JSON.

## Root Cause

- `.venv` contained an environment that was internally inconsistent across runtime
  rebuild attempts:
  - Python runtime drift (`3.14` vs intended `3.11`)
  - leftover wheel artifacts incompatible with target interpreter (`cp314` binary residues)
- installed `mediapipe` package line exposed `tasks` but not `solutions` API in this context
  during earlier checks, causing sidecar startup to pass import but fail at graph creation.

## Remediation Applied

### 1) Install explicit Python 3.11 runtime

```powershell
winget install --id Python.Python.3.11 --exact --silent --accept-package-agreements --accept-source-agreements
```

Resolved interpreter path:

- `C:\Users\leydi\AppData\Local\Programs\Python\Python311\python.exe`

### 2) Hard reset tracking venv and recreate with explicit interpreter

```powershell
Remove-Item -Recurse -Force .\.venv
powershell -ExecutionPolicy Bypass -File .\tools\setup_tracking_python_venv.ps1 `
  -VenvPath .\.venv `
  -PythonExe C:\Users\leydi\AppData\Local\Programs\Python\Python311\python.exe
```

### 3) Pin MediaPipe to sidecar-compatible line

```powershell
.\.venv\Scripts\python.exe -m pip install "mediapipe==0.10.11"
```

Observed effective runtime:

- `mediapipe 0.10.10`
- `has_solutions = True`

### 4) Re-pin user env to rebuilt venv Python

```powershell
[Environment]::SetEnvironmentVariable(
  "ANIMIQ_MEDIAPIPE_PYTHON",
  "D:\dbslxlvseefacedkfb\NativeAnimiq\.venv\Scripts\python.exe",
  "User")
```

## Verification

### Sanity script

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\mediapipe_sidecar_sanity.ps1
```

Report:

- `build/reports/mediapipe_sidecar_sanity_summary.txt`
- `Overall: PASS`

Key lines:

- `python_executable: PASS`
- `python_import_probe: PASS (mediapipe+cv2)`

### Sidecar frame emission smoke

Executed sidecar directly and captured first stdout packet:

```powershell
.\.venv\Scripts\python.exe .\tools\mediapipe_webcam_sidecar.py --camera 0 --fps 10 | Select-Object -First 1
```

Result:

- valid JSON frame payload emitted (`schema_version=1`, `frame_id=1`)
- confirms sidecar now reaches active capture/inference loop

## Operational Notes

- This was an environment/runtime remediation round, not HostCore source logic change.
- `TRACKING_MEDIAPIPE_NO_FRAME` can still occur for genuine camera contention, but in this
  incident the deterministic trigger was `mediapipe` API mismatch (`solutions` missing).
- If workspace root or venv path changes, `ANIMIQ_MEDIAPIPE_PYTHON` should be updated.
