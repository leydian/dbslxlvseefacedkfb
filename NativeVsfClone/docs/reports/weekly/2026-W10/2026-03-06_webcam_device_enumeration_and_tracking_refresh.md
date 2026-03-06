# Webcam Device Enumeration and Tracking Refresh Integration (2026-03-06)

## Summary

Implemented a safer, user-visible webcam detection flow for tracking configuration.

Primary outcomes:

1. HostCore webcam listing now uses real Windows camera enumeration instead of synthetic `Camera 0..N` placeholders.
2. Device keys remain numeric index strings (`"0"`, `"1"`, ...) to preserve MediaPipe sidecar argument compatibility.
3. WPF/WinUI webcam selectors now prefer available devices when the previous selection is missing or unavailable.
4. WPF tracking section entry and WinUI tracking start path now trigger device list refresh to reduce stale camera selection issues.

## Implementation Details

### 1) HostCore webcam enumeration contract

Updated:

- `host/HostCore/HostController.MvpFeatures.cs`

Changes:

- Replaced synthetic list generation in `GetAvailableWebcamDevices(...)` with Windows API enumeration:
  - `Windows.Devices.Enumeration.DeviceInformation.FindAllAsync(DeviceClass.VideoCapture)`
- Preserved public return model:
  - `WebcamDeviceOption(DeviceKey, DisplayName, IsAvailable)`
- Preserved sidecar-facing key strategy:
  - `DeviceKey` remains index-based string, derived from enumeration order.
- Added graceful failure/no-device behavior:
  - no devices: returns `Default Camera` with `IsAvailable=false`
  - enumeration exception: returns `Default Camera (enumeration failed)` and records host log

Rationale:

- Keeps runtime launch compatibility with `--camera "<index>"`.
- Avoids eager capture-handle open-probe in enumeration path, which can destabilize some virtual camera stacks.

### 2) WPF tracking webcam UX refresh

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Changes:

- `WebcamDeviceItem` extended with `IsAvailable`.
- Device list rendering now carries availability from HostCore and appends `- unavailable` label for unavailable entries.
- Selection fallback order on refresh:
  1. previously selected key
  2. first available item
  3. first item
- Added automatic webcam list refresh when entering `Tracking` section (when operation is idle and tracking is inactive).
- Pending placeholder list (`scan pending`) preserved for initial state before first active scan.

### 3) WinUI tracking webcam UX refresh

Updated:

- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- `WebcamDeviceItem` extended with `IsAvailable`.
- Device list rendering/selection fallback aligned with WPF behavior.
- Added refresh before `StartTracking` to use latest camera list at execution time.

## Behavioral Notes

- This update improves **device detection visibility** and **selection robustness**.
- It does not convert runtime start policy to silent fallback on camera-open failure; webcam runtime still reports start/frame readiness via existing tracking diagnostics/error codes.

## Verification

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -v minimal
dotnet build host/WpfHost/WpfHost.csproj -v minimal
dotnet build host/WinUiHost/WinUiHost.csproj -v normal
```

Results:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL at existing WinUI XAML compiler stage (`XamlCompiler.exe`/`MSB3073`) in current environment baseline

