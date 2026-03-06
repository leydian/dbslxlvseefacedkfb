# iFacialMocap Direct Tracking Integration (2026-03-06)

## Summary

This update implements direct iFacialMocap OSC ingestion in the host runtime path and wires it into native tracking submission (`nc_set_tracking_frame`) before each render tick.

Primary outcomes:

1. Host now receives tracking frames directly from iFacialMocap over UDP OSC (no external tracking sidecar dependency required).
2. Both host shells expose tracking controls (`port`, `start`, `stop`, `recenter`) with runtime status telemetry.
3. Tracking parser supports two runtime-detected packet styles (single-value and bundle/array-like), with diagnostics for parse/drop behavior.
4. Session persistence now stores tracking defaults/state (`listen port`, `stale timeout`, `last active`).

## Implementation Details

### 1) HostCore tracking contracts and runtime service

Updated:

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/HostController.cs`
- `host/HostCore/HostUiState.cs`
- `host/HostCore/NativeCoreInterop.cs`
- `host/HostCore/PlatformFeatures.cs`

Added:

- `host/HostCore/TrackingInputService.cs`

Key points:

- Added `NcTrackingFrame` interop + `nc_set_tracking_frame(ref NcTrackingFrame)` P/Invoke.
- Added tracking start/diagnostics contracts and service abstraction:
  - `TrackingStartOptions`
  - `TrackingDiagnostics`
  - `ITrackingInputService`
- `HostController.Tick()` now pushes latest tracking frame to native before render.
- `DiagnosticsSnapshot` now includes tracking diagnostics (`Tracking`).
- Added tracking diagnostics reconciliation logs:
  - `TrackingState`
  - `TrackingFormatDetected`
  - `TrackingParseError`

### 2) iFacialMocap OSC parsing strategy

Service: `TrackingInputService`

Default policy:

- listen port: `49983`
- stale timeout: `500ms`
- stale behavior: keep last valid frame (do not zero immediately), while exposing stale warning in diagnostics

Supported parse styles:

- `format-a`: address + single float (key extracted from address tail)
- `format-b`: bundle/array-like payloads:
  - OSC bundle element parsing
  - string/float pair extraction
  - multi-float address payloads for head vectors

MVP mapping:

- `EyeBlinkLeft` -> `blink_l`
- `EyeBlinkRight` -> `blink_r`
- `JawOpen` -> `mouth_open`
- `HeadYaw` / `HeadPitch` / `HeadRoll` -> quaternion conversion
- `HeadPosX` / `HeadPosY` / `HeadPosZ` -> head position

Safety/normalization:

- NaN/Inf rejection
- expression clamping (`[0,1]`)
- smoothing (EMA)
- identity fallback quaternion when head YPR is unavailable

### 3) WPF and WinUI control-surface parity

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Operator controls:

- `Tracking Port`
- `Start Tracking`
- `Stop Tracking`
- `Recenter`
- status text: `format`, `fps`, `last packet age`, `stale`, `received`, `dropped`, `parse errors`

Notes:

- Tracking start is manual only (no auto-start on initialize/load).
- WPF tracking panel was simplified to port-centric operation.

### 4) Session persistence update

Updated:

- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.MvpFeatures.cs`

Schema update:

- `SessionPersistenceModel.Version` -> `3`
- Added `TrackingInputSettings`:
  - `ListenPort`
  - `StaleTimeoutMs`
  - `LastActive`

Compatibility:

- v1/v2 session-state fallback normalization is preserved.

## Verification

Executed:

```powershell
dotnet build host\HostCore\HostCore.csproj -c Release
dotnet build host\WpfHost\WpfHost.csproj -c Release
dotnet build host\WinUiHost\WinUiHost.csproj -c Release
```

Result:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: blocked by existing environment/toolchain XAML compiler failure (`XamlCompiler.exe` exit code 1), consistent with current repository baseline

## Scope Notes

- This change intentionally focuses on MVP tracking channels (`blink/mouth/head`).
- Full ARKit channel mapping expansion is deferred.
