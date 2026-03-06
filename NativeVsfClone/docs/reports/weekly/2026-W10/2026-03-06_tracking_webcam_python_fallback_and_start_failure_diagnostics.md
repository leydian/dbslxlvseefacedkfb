# Tracking Webcam Python Fallback + Start Failure Diagnostics (2026-03-06)

## Summary

Implemented webcam tracking startup hardening for MediaPipe sidecar launch on
Windows hosts where default `python` resolution is unreliable.

Primary objective:

1. reduce opaque `Start tracking failed: Io` outcomes,
2. surface actionable failure cause directly in UI dialogs,
3. preserve existing strict startup validation behavior.

## Problem

Observed operator symptom:

- tracking start popup reported only `Start tracking failed: Io`.

Runtime diagnosis showed multiple hidden root causes behind the same code:

- Python executable not invokable in current environment,
- sidecar exited before first frame,
- sidecar alive but frame stream not ready.

Existing UX exposed insufficient context to recover quickly.

## Implementation

### 1) HostCore Python executable resolution fallback chain

Updated:

- `host/HostCore/TrackingInputService.cs`

Behavior:

1. build ordered Python candidates:
   - `VSFCLONE_MEDIAPIPE_PYTHON`
   - `py -3`
   - `.venv\Scripts\python.exe` from `AppContext.BaseDirectory`
   - `.venv\Scripts\python.exe` from `Environment.CurrentDirectory`
   - `python`
2. probe each candidate with `--version`.
3. select first successful candidate and launch sidecar with it.
4. if all fail, return invalid launch config with:
   - attempted candidate list,
   - explicit action guidance (`VSFCLONE_MEDIAPIPE_PYTHON` or `setup_tracking_python_venv.ps1`).

Outcome:

- startup no longer depends on one implicit executable token.
- failure diagnostics become deterministic and operator-readable.

### 2) Startup warmup error classification split

Updated:

- `host/HostCore/TrackingInputService.cs`

During sidecar warmup:

- if sidecar exits before first frame:
  - `LastErrorCode = TRACKING_MEDIAPIPE_START_FAILED`
  - status includes sidecar exit code and latest stderr when available.
- if sidecar remains alive but frame not available:
  - `LastErrorCode = TRACKING_MEDIAPIPE_NO_FRAME`.

Outcome:

- separates process-start class failures from frame-readiness failures.

### 3) WPF/WinUI startup failure dialog uplift

Updated:

- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

`StartTracking` failure dialog now includes:

- result code (`NcResultCode`)
- `TrackingDiagnostics.LastErrorCode`
- `TrackingDiagnostics.StatusMessage`
- one-line mapped remediation per error class

Result:

- operators get immediate next action without opening source or logs.

### 4) Shared hint catalog update

Updated:

- `host/HostCore/TrackingErrorHintCatalog.cs`

`TRACKING_MEDIAPIPE_START_FAILED` hint now explicitly points to:

- `VSFCLONE_MEDIAPIPE_PYTHON`
- `tools/setup_tracking_python_venv.ps1`

## Verification

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release
dotnet build host/WpfHost/WpfHost.csproj -c Release
dotnet build host/WinUiHost/WinUiHost.csproj -c Release
```

Results in current environment:

- all three builds failed at restore stage with `NU1301` due blocked access to `https://api.nuget.org/v3/index.json` (`api.nuget.org:443`).
- compile-stage validation could not be completed in this sandboxed network condition.

## Impact

- Improves first-response recovery flow for webcam tracking startup issues.
- Reduces ambiguity around `Io` startup failures by surfacing concrete reason classes.
- Maintains strict startup policy while improving fallback resiliency and UX explainability.
