# Tracking MediaPipe START_FAILED User Env Pin Fix (2026-03-06)

## Summary

Resolved a real runtime failure where webcam tracking startup failed with:

- `ErrorCode: TRACKING_MEDIAPIPE_START_FAILED`
- `Status: mediapipe sidecar exited before first frame (code=2)`

Root cause in this machine context was Python resolution mismatch:

- default `py -3` and `python` were not invokable,
- project-local `.venv` Python was healthy and could import `mediapipe` and `cv2`.

The fix pinned the runtime Python explicitly via user environment variable.

## Observed Failure Context

User-facing popup (WPF host):

- `Start tracking failed: Io`
- `LastErrorCode: TRACKING_MEDIAPIPE_START_FAILED`
- `Status: mediapipe sidecar exited before first frame (code=2)`

Sidecar behavior contract (`tools/mediapipe_webcam_sidecar.py`):

- exit code `2` is returned when dependency import fails (`mediapipe`/`cv2`).

## Diagnostics Performed

Executed checks in local workspace on 2026-03-06:

```powershell
py -3 --version
python --version
py -3 -c "import mediapipe, cv2; print('ok')"
python -c "import mediapipe, cv2; print('ok')"
.\NativeVsfClone\.venv\Scripts\python.exe --version
.\NativeVsfClone\.venv\Scripts\python.exe -c "import mediapipe, cv2; print('venv import ok')"
```

Observed:

- `py -3`: not available/invocation failure
- `python`: not available/invocation failure
- `.venv\Scripts\python.exe`: `PASS`
- `.venv` import probe (`mediapipe`, `cv2`): `PASS`

## Applied Remediation

Pinned MediaPipe runtime executable to the verified project venv:

```powershell
[Environment]::SetEnvironmentVariable(
  "VSFCLONE_MEDIAPIPE_PYTHON",
  "D:\dbslxlvseefacedkfb\NativeVsfClone\.venv\Scripts\python.exe",
  "User")
```

Verification command:

```powershell
[Environment]::GetEnvironmentVariable("VSFCLONE_MEDIAPIPE_PYTHON", "User")
```

Expected value:

- `D:\dbslxlvseefacedkfb\NativeVsfClone\.venv\Scripts\python.exe`

## Post-Fix Validation

Executed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\mediapipe_sidecar_sanity.ps1
```

Report:

- `build/reports/mediapipe_sidecar_sanity_summary.txt`

Key results:

- `sidecar_script: PASS`
- `python_executable: PASS`
- `python_import_probe: PASS (mediapipe+cv2)`
- `Overall: PASS`

## Operational Notes

- This fix is machine-level runtime configuration, not source-code behavior change.
- Existing HostCore fallback logic remains unchanged.
- If workspace path changes, `VSFCLONE_MEDIAPIPE_PYTHON` must be updated to the new absolute path.
