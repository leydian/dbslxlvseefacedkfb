# Tracking IFM Native Text Parser and XAV2 Base-Only Shadow Inference (2026-03-06)

## Summary

This change resolves the live iFacial receive failure where packets arrived but were classified as parse failures, and also hardens native XAV2 shadow-pass inference for legacy base-only pass declarations.

Primary outcomes:

1. Tracking runtime now parses native iFacial text payloads using `key&value|...` and head-section `|=` forms.
2. Delimited IFM compatibility is expanded to accept `-` separators (`key-value`) in addition to `:`/`=`.
3. WPF tracking diagnostics snapshot path now force-syncs from `TrackingInputService`, reducing stale UI counters when render tick gating occurs.
4. Native renderer infers shadow pass for parity-family XAV2 materials when pass flags are base-only, preventing shadow dropout on legacy exports.

## Problem

Observed runtime symptoms:

- Tracking UI: `tracking=on`, packets initially non-zero, but `TRACKING_PARSE_FAILED[...]` with packet signature beginning `Tracking...`.
- Captured payload from iPhone sender:
  - `trackingStatus-1|cheekPuff-2|...` (`key-value` pairs with `|` delimiters)
- Existing delimited parser focused on `key:value` / `key=value` and did not fully accept this variant.
- In parallel, certain XAV2 exports with base-only pass declarations could miss shadow path enablement despite valid parity-family context.

## Implementation Details

### 1) IFM native text parser path (HostCore)

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- Added `TryParseIfmNativePacket(...)`:
  - blendshape section: `key&value|key&value|...`
  - optional head section split by `|=` and parsed as `key&value&key&value...`
  - channel mapping still routes through existing `AddIfmUpdate(...)` normalization/allowlist logic.
- Wired native text parse attempt before generic delimited fallback in `TryParseIfmPacket(...)`.

Expected format labels:

- `ifm-v1` / `ifm-v2` remain unchanged and are selected by existing version extraction.

### 2) Delimited IFM compatibility widening

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- `IfmDelimitedPairRegex` updated to accept `-` as key/value separator:
  - from `[:=]` to `[:=\-]`
- `IfmVersionRegex` similarly widened to parse `version-2` style payloads.

Result:

- Payloads like `trackingStatus-1|...` are now consumed by delimited IFM parse path when native `&` form is absent.

### 3) Tracking diagnostics snapshot synchronization

File:

- `host/HostCore/HostController.cs`

Changes:

- `BuildSnapshot()` now calls `RefreshTrackingDiagnosticsFromService()` before constructing `DiagnosticsSnapshot`.
- New helper pulls fresh diagnostics from `ITrackingInputService` each snapshot publish.
- Preserves existing native-submit error continuity (`NC_SET_*`) when service-level `LastErrorCode` is empty.
- Calls `ReconcileTrackingDiagnostics()` after sync to keep log/event behavior consistent.

Result:

- Runtime/Tracking diagnostic text is less likely to remain stale under UI/tick gating conditions.

### 4) XAV2 base-only pass shadow inference (NativeCore)

File:

- `src/nativecore/native_core.cpp`

Changes:

- Added `base_only_pass_declared` detection for pass token sets containing only base/main/forward without depth/shadow/outline/emission markers.
- For XAV2 parity-family materials, infer shadow pass enablement under this condition:
  - `material.enable_shadow_pass = true`
  - fallback reason: `xav2_pass_flags_base_only_shadow_inferred`

Result:

- Legacy XAV2 exports with sparse pass declarations retain expected shadow caster behavior.

## Validation

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release --no-restore
dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -Configuration Release -RuntimeIdentifier win-x64 -NoRestore -RunWpfLaunchSmoke:$false
```

Results:

- `HostCore`: PASS (`0 warnings`, `0 errors`)
- `WpfHost`: PASS (`0 warnings`, `0 errors`)
- WPF dist publish: PASS

Runtime confirmation (operator repro):

- Tracking status transitioned to:
  - `format=ifm-v1`
  - `packets=496`
  - `parse_err=0`
  - high IFM confidence (`ifacial=0.98`)

## Compatibility and Risk Notes

- No public API schema changes.
- Parser widening is additive; existing OSC/IFM paths remain intact.
- Native shadow inference is scoped to XAV2 + parity-family + base-only pass declarations to avoid broad side effects.
