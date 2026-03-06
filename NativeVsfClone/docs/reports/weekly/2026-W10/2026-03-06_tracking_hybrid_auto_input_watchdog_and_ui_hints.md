# Tracking Hybrid Auto + No-Input Watchdog + UI Hint Hardening (2026-03-06)

## Summary

This change hardens host-side tracking operability for the "avatar loaded but tracking does not move" scenario by introducing a default `HybridAuto` tracking source mode, adding explicit no-input watchdog diagnostics, and exposing source-age telemetry and stronger hint text in both WPF and WinUI.

Primary outcomes:

1. Tracking source default is now `HybridAuto` (`OSC iFacial + Webcam fallback`) instead of OSC-only.
2. Runtime emits explicit no-input diagnostics after startup grace period when no usable packets arrive.
3. Tracking status UI now surfaces per-source packet age (`ifacial_age`, `webcam_age`) and improved actionable hints.
4. Existing explicit source modes (`OscIfacial`, `WebcamMediapipe`) are preserved with deterministic routing behavior.

## Implementation Details

### 1) Tracking contract and defaults

Updated:

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.cs`

Key points:

- Added `TrackingSourceType.HybridAuto = 2`.
- `TrackingDiagnostics` extended with:
  - `IfacialPacketAgeMs`
  - `WebcamPacketAgeMs`
- Session tracking defaults and normalization fallback switched to `HybridAuto`.
- HostController initial diagnostics source default aligned to `HybridAuto`.

### 2) Runtime behavior in `TrackingInputService`

Updated:

- `host/HostCore/TrackingInputService.cs`

Behavior changes:

- Added startup no-input watchdog threshold:
  - `NoActiveInputWarnDelayMs = 3000`
- Added source-aware no-input warning codes:
  - `TRACKING_NO_ACTIVE_INPUT_SOURCE`
  - `TRACKING_IFACIAL_NO_PACKET`
  - `TRACKING_WEBCAM_RUNTIME_UNAVAILABLE`
  - `TRACKING_WEBCAM_NO_FRAME`
- Added source packet-age computation helper and applied it to diagnostics snapshots.
- Added no-input warning application policy:
  - only after startup grace period
  - clears warning automatically when valid source packet arrives
  - does not override non no-input operational errors

Source routing policy:

- `WebcamMediapipe` mode:
  - webcam-only consumption.
- `OscIfacial` mode:
  - OSC-only consumption, no webcam arbitration usage.
- `HybridAuto` mode:
  - OSC + webcam runtime startup attempt.
  - webcam startup failure is non-fatal for tracking start.
  - fallback/arbitration behavior remains active.

State hygiene:

- On `Stop()`, runtime source is reset to `none`.
- Diagnostics explicitly publish `ActiveSource=none` when not active.

### 3) WPF/WinUI control-surface and diagnostics text

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Key points:

- Tracking source combo now includes:
  - `Auto (OSC + Webcam)`
  - `OSC (iFacialMocap)`
  - `Webcam (MediaPipe)`
- Start/config mapping updated to resolve 3-way source selection.
- Session-default UI restoration updated to map enum values to the new 3-item combo order.
- Tracking status text now prints:
  - `ifacial_age=<ms>`
  - `webcam_age=<ms>`
- Error hint mapping expanded for new no-input/runtime-unavailable codes.

## Verification

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release --no-restore
dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore
dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64 --no-restore
```

Results:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL in current environment baseline (`XamlCompiler.exe` / `MSB3073`), consistent with prior workspace status and not introduced by this tracking change.

## Operator Impact

- If tracking is started without iFacial sender and without usable webcam runtime/frames, the UI now reports a specific failure reason instead of a generic "not moving" state.
- Default startup behavior is more resilient for mixed operator setups by preferring hybrid auto source strategy.
