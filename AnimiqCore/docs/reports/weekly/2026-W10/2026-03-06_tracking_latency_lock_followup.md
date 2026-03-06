# Tracking Latency/Lock Follow-up (2026-03-06)

## Summary

This follow-up hardens the hybrid tracking runtime for low-latency operation by adding:

1. explicit source-lock control (`Auto` / `iFacial locked` / `webcam locked`),
2. runtime latency profiles (`LowLatency` / `Balanced` / `Stable`),
3. stage-level latency diagnostics (`capture`, `parse`, `smooth`, `submit`), and
4. sidecar packet-contract additions for timestamped latency estimation.

The implementation focuses on HostCore runtime behavior and diagnostics contract expansion.

## Detailed Changes

### 1) Tracking contract and start-option expansion

Updated:

- `host/HostCore/HostInterfaces.cs`

Added enums:

- `TrackingSourceLockMode`
  - `Auto`
  - `IfacialLocked`
  - `WebcamLocked`
- `TrackingLatencyProfile`
  - `LowLatency`
  - `Balanced`
  - `Stable`

Extended contracts:

- `TrackingStartOptions`
  - `SourceLockMode`
  - `LatencyProfile`
- `TrackingDiagnostics`
  - `LatencyAvgMs`
  - `LatencyP95Ms`
  - `CaptureStageMs`
  - `ParseStageMs`
  - `SmoothStageMs`
  - `SubmitStageMs`
  - `SourceLockMode`
  - `SwitchBlockedReason`

### 2) Session persistence and migration compatibility

Updated:

- `host/HostCore/PlatformFeatures.cs`

Changes:

- `TrackingInputSettings` persists:
  - source lock mode
  - latency profile
- `SessionPersistenceModel.Version` raised to `6`.
- normalization and legacy fallback paths default to:
  - `SourceLockMode=Auto`
  - `LatencyProfile=Balanced`

Compatibility:

- previous session models are normalized without breaking existing runtime startup.

### 3) Host configuration routing

Updated:

- `host/HostCore/HostController.MvpFeatures.cs`

Changes:

- tracking config API now accepts optional:
  - `sourceLockMode`
  - `latencyProfile`
- tracking config logs now include lock/profile values for operator traceability.

### 4) Runtime arbitration and latency instrumentation

Updated:

- `host/HostCore/TrackingInputService.cs`

Source arbitration behavior:

- lock-aware arbitration:
  - lock modes force active source selection.
  - blocked automatic switches surface explicit reason:
    - `source-lock:ifacial`
    - `source-lock:webcam`

Latency profile tuning:

- `LowLatency`:
  - higher EMA responsiveness
  - faster fallback/recovery thresholds
- `Balanced`:
  - default current operating profile
- `Stable`:
  - lower EMA responsiveness
  - conservative fallback/recovery thresholds

Latency diagnostics:

- rolling latency window sampled and exposed as:
  - average (`LatencyAvgMs`)
  - 95th percentile (`LatencyP95Ms`)
- stage metrics tracked with smoothed values:
  - capture
  - parse
  - smooth
  - submit

### 5) MediaPipe sidecar packet contract update

Updated:

- `tools/mediapipe_webcam_sidecar.py`
- `host/HostCore/TrackingInputService.cs`

New packet fields:

- `schema_version`
- `source_ts_unix_ms`

Host parser behavior:

- validates `schema_version` presence/value.
- consumes source timestamp when available to improve capture-stage estimation.

## Verification Snapshot

Executed:

```powershell
dotnet build host\HostCore\HostCore.csproj -c Release --no-restore
dotnet build host\WpfHost\WpfHost.csproj -c Release --no-restore
```

Result:

- build verification is blocked in current environment by network-restricted NuGet repository-signature fetch:
  - `NU1301`
  - target: `api.nuget.org:443`

## Notes

- This report is scoped to tracking-latency/lock hardening changes and sidecar packet-contract updates.
- Existing unrelated workspace modifications are intentionally out of scope for this report.
