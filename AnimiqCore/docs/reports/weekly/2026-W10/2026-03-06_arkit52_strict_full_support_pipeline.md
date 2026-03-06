# ARKit52 Strict Full-Support Pipeline (VRM + MIQ) (2026-03-06)

## Summary

Implemented end-to-end ARKit52 support hardening across HostCore and NativeCore with strict 1:1 channel policy and missing-channel warning behavior.

Primary outcomes:

1. Host tracking now uses a shared canonical ARKit52 channel source and publishes runtime ARKit52 coverage diagnostics.
2. Native avatar load path now builds ARKit52 expression bindings from blendshape payloads for both VRM and MIQ paths.
3. Missing ARKit52 channels no longer fail avatar load; they emit warning telemetry and remain safely inactive.
4. Legacy blink/jaw/smile alias fallbacks are disabled when ARKit52 binding mode is active to avoid mixed-mode overrides.

## Detailed Changes

### 1) HostCore canonical channel source + diagnostics

Added:

- `host/HostCore/Arkit52Channels.cs`

Updated:

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/TrackingInputService.cs`
- `host/HostCore/HostController.cs`

Key changes:

- Introduced a single canonical ARKit52 channel list and shared normalization helper.
- Replaced local ARKit list duplication in tracking service with shared source.
- Extended `TrackingDiagnostics` with ARKit52 coverage fields:
  - `Arkit52SubmittedCount`
  - `Arkit52MissingCount`
  - `Arkit52MissingKeys`
- `HostController.Tick()` now computes ARKit52 submit/missing coverage each frame from expression payloads before native submission.

### 2) WPF/WinUI tracking visibility updates

Updated:

- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

Key changes:

- Tracking status text now displays ARKit52 runtime coverage:
  - `arkit52={submitted}/52`
  - `missing={count}`
- Runtime diagnostics dump text now includes the same ARKit52 coverage fields.

### 3) NativeCore strict ARKit52 binding construction

Updated:

- `src/nativecore/native_core.cpp`

Key changes:

- Added ARKit52 canonical channel table in native runtime.
- Added avatar-load binding build pass:
  - scans mesh blendshape frame names
  - creates/normalizes one expression per ARKit52 channel
  - binds only exact channel-name matches (case-insensitive exact-name match)
  - clears stale binds and rebuilds deterministically
- Added warning path for incomplete avatar channel coverage:
  - warning code: `W_ARKIT52_MISSING_BIND`
  - load behavior: proceed (non-fatal), with explicit warning summary

### 4) Native expression application precedence update

Updated:

- `src/nativecore/native_core.cpp`

Key changes:

- Added ARKit52-mode detection (`HasArkit52ExpressionBindings(...)`).
- In ARKit52 mode, disabled legacy fallback aliases for:
  - blink average fallback
  - jaw-open viseme fallback
  - smile/joy fallback
- Result: direct ARKit channel weights remain authoritative when strict ARKit52 bindings are present.

## Behavior Notes

- Policy is strict 1:1 by channel name; no alias mapping is applied for ARKit52 channel-to-morph matching.
- Missing channel bindings remain allowed and visible via warning + diagnostics, matching non-fatal operator workflow.
- This slice focuses on runtime binding/application and diagnostics; it does not introduce loader-side schema/API contract changes.

## Verification

Attempted:

```powershell
dotnet build NativeAnimiq/host/HostCore/HostCore.csproj -c Release
dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -c Release
```

Current environment result:

- blocked by NuGet restore/network constraint (`NU1301`, `api.nuget.org` unreachable in sandboxed environment)

Static consistency checks completed:

- all new symbols resolve in local text scan
- host/native integration points are wired:
  - tracking expression payload -> `nc_set_expression_weights(...)`
  - native ARKit52 bind construction -> avatar load path
