# Runtime Common-Cause Triage Classifier (WPF) + Post-Load Snapshot Log (2026-03-06)

## Summary

Implemented a deterministic common-cause triage path for the clustered symptom set:

- arm motion appears non-functional
- realtime shadow appears missing
- facial expression appears non-functional

This pass adds a single prioritized classifier in host runtime flow and surfaces the same result in WPF runtime diagnostics so operators can classify failures immediately after avatar load.

Priority order is fixed to reduce ambiguous triage loops:

1. runtime binary provenance mismatch/stale
2. native submit failure (`NC_SET_*`)
3. payload/policy gate signals (expression/arm/shadow)

## Problem Context

Previous diagnostics required manual cross-reading of multiple signals:

- runtime path/staleness fields
- tracking/native submit error code
- avatar warning codes and pass summary

As a result, clustered failures often re-entered long triage cycles despite existing evidence.

## Implementation

### 1) Host post-load triage snapshot emission

Updated:

- `host/HostCore/HostController.cs`

Changes:

- Added delayed post-load triage schedule (`~1.2s`) on successful `LoadAvatar`.
- Added one-shot triage log source: `CommonCauseTriage`.
- Log payload includes:
  - `class`
  - `reason`
  - `format`
  - `expressions`
  - `arm`
  - `shadow`
  - `tracking_err`
  - `warning`
- Added deterministic class selection:
  - `runtime_binary_mismatch`
  - `native_submit_failure`
  - `payload_policy_gate`
  - `none_detected`

### 2) Common signal extraction helpers (arm/shadow)

Updated:

- `host/HostCore/HostController.cs`

Changes:

- Added marker-based warning extraction for:
  - `ARM_POSE_*`
  - `SHADOW_DISABLED_*`
- Added fallback shadow signal from `ActivePasses` when explicit warning marker is absent.

### 3) WPF runtime diagnostics line for immediate operator visibility

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Changes:

- Added `CommonCauseTriage: ...` line in runtime diagnostics text.
- Mirrors host-side priority and classification rules.
- Displays concise reason and key evidence fields (`format`, `expressions`, `arm`, `shadow`, `warning`).

## Behavioral Result

After avatar load, the host now emits one clear triage classification with explicit reason.

Expected operator flow:

1. open runtime diagnostics
2. read `CommonCauseTriage` line
3. branch directly to one of:
   - runtime republish/relaunch path
   - native submit failure investigation
   - avatar payload/policy warning triage

## Verification Notes

Attempted local build verification in this workspace, but restore/build was environment-blocked:

- missing local NuGet mirror path (`.../NativeVsfClone/build/nuget-mirror`)
- unavailable `nuget.org` service index

No API signature changes were introduced in this pass.

## Risk / Compatibility

- Additive diagnostics/logging only.
- Existing telemetry/log consumers may observe new `CommonCauseTriage` source entries.
- Classification is intentionally conservative; it prioritizes deterministic triage speed over deep multi-cause decomposition in a single pass.
