# Tracking Local IPv4 Hint + Hide Toggle (WPF/WinUI) (2026-03-06)

## Summary

Implemented operator-facing local IPv4 guidance in the tracking section so iFacialMocap destination setup can be completed without leaving the host UI.

Both WPF and WinUI now show:

- recommended local IPv4 (copy-ready)
- all detected local IPv4 candidates
- hide/show toggle for the hint panel

The toggle state is persisted in session state and restored on next launch.

## Implementation Details

### 1) Session persistence extension

Updated:

- `host/HostCore/PlatformFeatures.cs`

Changes:

- `SessionPersistenceModel` extended with:
  - `UiShowTrackingIpv4Hint: bool`
- model version bumped to `9`
- defaults:
  - `UiShowTrackingIpv4Hint = true`
- normalization policy:
  - older persisted versions (`< 9`) migrate to `true`

### 2) HostCore IPv4 hint API

Updated:

- `host/HostCore/HostController.MvpFeatures.cs`

Added:

- `SetUiTrackingIpv4HintVisible(bool visible)`
  - persists toggle preference
- `GetLocalIpv4Hint()`
  - returns `(recommendedIpv4, allIpv4)` tuple for UI
- local IPv4 enumeration utility:
  - scans active network interfaces
  - excludes loopback/tunnel
  - keeps IPv4 only (`AddressFamily.InterNetwork`)
  - de-duplicates candidate list

### 3) WPF tracking UX

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

Added:

- tracking IPv4 hint panel (`Recommended IPv4`, `All IPv4`)
- `IPv4 복사` button
- `IPv4 안내 숨기기/보기` toggle button

Behavior:

- hint text refreshes from HostCore
- copy button is enabled only when recommended IPv4 exists
- toggle state is persisted via `SetUiTrackingIpv4HintVisible(...)`
- persisted state is restored in `ApplySessionDefaultsToUi()`

### 4) WinUI tracking UX parity

Updated:

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Added same capabilities as WPF:

- IPv4 hint panel
- copy button
- hide/show toggle
- persisted state restore on startup

## Verification

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release
dotnet build host/WpfHost/WpfHost.csproj -c Release
dotnet build host/WinUiHost/WinUiHost.csproj -c Release
```

Results:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL at existing environment baseline (`XamlCompiler.exe` / `MSB3073`)

## Operator Impact

- iFacialMocap destination IP setup is now discoverable directly inside tracking UI.
- accidental exposure concern is handled by a persistent hide toggle.
- restart does not reset operator preference.
