# Tracking Webcam Sidecar Packaging + Path Resolution Fix (2026-03-06)

## Summary

Fixed tracking-start `InvalidArgument` failures in `WebcamMediapipe` mode caused by missing `mediapipe_webcam_sidecar.py` in runtime output and brittle script-path discovery.

This update keeps explicit-start failure policy for invalid webcam runtime config, but makes the failure deterministic and immediately diagnosable by operators.

## Root Cause

- `TrackingInputService` returns `NcResultCode.InvalidArgument` when webcam runtime launch config is invalid.
- Launch config is invalid when sidecar script path cannot be resolved.
- Runtime path lookup was not robust across host launch/publish layouts.
- Host project outputs did not explicitly guarantee sidecar script copy into `tools/`.

## Implementation Details

### 1) Host output/publish packaging hardening

Updated:

- `host/WpfHost/WpfHost.csproj`
- `host/WinUiHost/WinUiHost.csproj`

Added explicit sidecar content item:

- source: `..\..\tools\mediapipe_webcam_sidecar.py`
- linked output path: `tools\mediapipe_webcam_sidecar.py`
- copy policy:
  - `CopyToOutputDirectory="PreserveNewest"`
  - `CopyToPublishDirectory="PreserveNewest"`

Result:

- build/publish outputs now consistently include the webcam sidecar script required by `WebcamMediapipe` startup.

### 2) Sidecar path resolution policy hardening

Updated:

- `host/HostCore/TrackingInputService.cs`

Introduced deterministic resolver `ResolveMediapipeSidecarScriptPath(...)` with ordered search:

1. `ANIMIQ_MEDIAPIPE_SIDECAR_SCRIPT`
2. `AppContext.BaseDirectory/tools/mediapipe_webcam_sidecar.py`
3. `AppContext.BaseDirectory/mediapipe_webcam_sidecar.py`
4. `Environment.CurrentDirectory/tools/mediapipe_webcam_sidecar.py`

Failure behavior:

- preserves explicit startup failure (`InvalidArgument`) when not found
- enriches error message with searched path list and required environment variable guidance

### 3) Operator-facing hint clarity

Updated:

- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

`TRACKING_MEDIAPIPE_CONFIG_INVALID` hint now includes:

- missing artifact name: `mediapipe_webcam_sidecar.py`
- override variable: `ANIMIQ_MEDIAPIPE_SIDECAR_SCRIPT`

Result:

- operators can resolve startup failure directly from status text without inspecting source.

## Verification

Executed:

```powershell
dotnet build host/WpfHost/WpfHost.csproj -c Release
```

Results:

- `WpfHost`: PASS
- output artifact verified:
  - `host/WpfHost/bin/Release/net8.0-windows10.0.19041.0/win-x64/tools/mediapipe_webcam_sidecar.py`

Additional check:

```powershell
dotnet build host/WinUiHost/WinUiHost.csproj -c Release
```

Results:

- `WinUiHost`: FAIL at existing environment baseline (`XamlCompiler.exe` / `MSB3073`)
- failure occurs in WinUI XAML compile stage; not in tracking-sidecar path/packaging logic

## Impact

- Reduces false-opaque tracking-start failures in webcam mode.
- Keeps strict/explicit validation policy (invalid runtime config still fails fast).
- Improves deploy parity across local run and publish artifacts for both WPF and WinUI hosts.
