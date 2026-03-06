# Tracking IFM Key-Sample Telemetry and browOuterUp Alias Completion (2026-03-06)

## Summary

This update closes a late-stage iFacial tracking operability gap where packet receive/parse was healthy but expression coverage remained partial due to residual key-name mismatch.

Primary outcomes:

1. Host status text now surfaces IFM key samples for both accepted and dropped channels.
2. Remaining ARKit52 gap from `browOuterUpLeft/Right` compact sender variants is covered.
3. Operator can now diagnose key-shape mismatch without external packet-capture tooling.

## Problem

Observed runtime evidence:

- `format=ifm-v1`, `packets` increasing, `parse_err=0`
- `arkit52=50/52`
- drop samples included:
  - `browouterupl`
  - `browouterupr`

This indicated transport/parser health with a residual alias stem mismatch.

## Implementation Details

### 1) Tracking diagnostics contract expansion

File:

- `host/HostCore/HostInterfaces.cs`

Changes:

- Added to `TrackingDiagnostics`:
  - `IfmAcceptedKeySample`
  - `IfmDroppedKeySample`

Purpose:

- Carry compact key-frequency summaries to host UI/runtime diagnostics.

### 2) IFM key telemetry capture in runtime parser path

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- Added key counters:
  - `_ifmAcceptedKeyCounts`
  - `_ifmDroppedKeyCounts`
- `AddIfmUpdate(...)` converted to instance method and now records:
  - accepted keys on successful normalization/mapping
  - dropped keys on normalization reject
- Added helpers:
  - `RecordIfmKey(...)`
  - `BuildIfmKeySampleSummary(...)`
- `GetDiagnostics()` now publishes top key samples into:
  - `IfmAcceptedKeySample`
  - `IfmDroppedKeySample`
- `ResetRuntimeState()` clears telemetry counters.

### 3) Residual alias completion (`browOuterUp*`)

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- Added `browouterup` to compact left/right expansion stems (`IfmLeftRightAliasStems`).

Effect:

- `browouterupl` -> `browouterupleft`
- `browouterupr` -> `browouterupright`

### 4) WPF/WinUI status/runtime text surfacing

Files:

- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- Tracking status line now appends:
  - `ifm_keys_ok={...}`
  - `ifm_keys_drop={...}`
- Runtime diagnostics `Tracking:` row includes same fields.

## Validation

Executed:

```powershell
dotnet build D:\dbslxlvseefacedkfb\NativeAnimiq\host\HostCore\HostCore.csproj -c Release --no-restore
dotnet build D:\dbslxlvseefacedkfb\NativeAnimiq\host\WpfHost\WpfHost.csproj -c Release --no-restore
```

Results:

- HostCore: PASS (`0 warnings`, `0 errors`)
- WpfHost: PASS (`0 warnings`, `0 errors`)

Runtime evidence (pre-fix snapshot):

- `arkit52=50/52`
- `ifm_keys_drop=browouterupl:...,browouterupr:...`

Expected post-fix behavior:

- `browouterupl/upr` no longer present in `ifm_keys_drop`
- ARKit52 coverage reaches full sender-supported set (`52/52` under equivalent payload conditions)
