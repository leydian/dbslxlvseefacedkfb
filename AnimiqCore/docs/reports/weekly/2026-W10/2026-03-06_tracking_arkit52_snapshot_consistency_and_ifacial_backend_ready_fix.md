# Tracking ARKit52 Snapshot Consistency + iFacial BackendReady Alignment Fix (2026-03-06)

## Summary

Resolved a diagnostics inconsistency where ARKit52 resolution metrics were computed in `Tick()` but later overwritten by service snapshot refresh, causing runtime UI to repeatedly show `arkit52=0/52` even while iFacial packets were actively received.

Also aligned iFacial receive-path diagnostics so `backend_ready` (`ModelSchemaOk`) reflects actual parse/mapping health:

- success path (`ifacial-active`) now explicitly reports `ModelSchemaOk=true`
- parse/no-mapped failure paths explicitly report `ModelSchemaOk=false`

## Problem Context

Observed runtime symptom cluster:

- `Tracking: active=True`, `packets` increasing, `ifm_keys_ok` populated
- but `arkit52=0/52`, `missing=52` stayed fixed
- and `backend_ready=false` sometimes persisted despite active iFacial traffic

Root causes:

1. `HostController.Tick()` computed `Arkit52ResolutionSummary` and patched `_trackingDiagnostics`, but `BuildSnapshot()` calls `RefreshTrackingDiagnosticsFromService()`, which pulled a fresh service snapshot and replaced the patched ARKit52 counters.
2. iFacial receive loop did not consistently set `ModelSchemaOk` on successful mapped frames, so the field could remain stale/false from prior failure states.

## Implementation

### 1) Preserve ARKit52 summary across snapshot refresh

Updated:

- `host/HostCore/HostController.cs`

Changes:

- added `_lastArkit52Summary` cache field
- store latest summary each tick after ARKit52 payload resolution
- clear cached summary on tracking lifecycle boundaries (`StartTracking`, `StopTracking`, `RecenterTracking`, `Shutdown`)
- introduced `ApplyArkit52Summary(...)` helper for deterministic field patching
- in `RefreshTrackingDiagnosticsFromService()`, re-apply cached ARKit52 summary after service diagnostics pull

Result:

- runtime snapshot no longer regresses ARKit52 counters to service defaults after tick-time computation
- UI/runtime diagnostics now preserve the most recent computed strict/fallback/missing/quality metrics

### 2) Align iFacial `ModelSchemaOk` with actual runtime state

Updated:

- `host/HostCore/TrackingInputService.cs`

Changes in OSC receive loop:

- parse failure path (`TryParsePacket` false): set `ModelSchemaOk=false`
- no-mapped-channels path (`ApplyUpdates` false): set `ModelSchemaOk=false`
- successful mapped update path (`ifacial-active`): set `ModelSchemaOk=true`

Result:

- `backend_ready` now tracks active iFacial parse/mapping outcome instead of stale prior state

## Verification

Executed:

```powershell
dotnet build .\host\WpfHost\WpfHost.csproj -c Release
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild
```

Observed:

- build: PASS (HostCore + WpfHost)
- WPF publish: PASS
- launch smoke: PASS

Expected runtime validation after deployment:

- with active iFacial packets, `arkit52` counters move from fixed `0/52` to resolved values
- `backend_ready=true` on successful receive/mapping frames
- parse/no-mapped failures still surface `backend_ready=false` with existing error codes

## Compatibility / Risk

- no external API contract changes
- diagnostics semantics only (state consistency fix)
- low risk: changes are isolated to host-side diagnostics merge and iFacial receive-path status flags
