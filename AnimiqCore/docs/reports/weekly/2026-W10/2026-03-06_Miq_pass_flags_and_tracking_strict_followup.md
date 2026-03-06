# MIQ Pass Flags Recovery + Strict Tracking Wrapper Follow-up (2026-03-06)

## Summary

This follow-up aligns runtime safety defaults and release-readiness operator flow after additional validation runs.

The update covers:

- MIQ static skinning safety default clarification in native runtime,
- fail-safe base-pass recovery when non-canonical pass flags disable all draws,
- strict tracking wrapper passthrough for version/baseline skip switches,
- tracking fuzz tool target framework alignment,
- refreshed VRM probe/gate evidence with expanded MToon diagnostics fields.

## Changed

### 1) Native runtime safety and draw-pass guard (`src/nativecore/native_core.cpp`)

- Static skinning auto-mode policy for MIQ is explicitly safety-first:
  - default remains OFF unless explicitly forced.
- Added pass-flag fail-safe for MIQ material path:
  - when all pass flags resolve to disabled (`base/depth/shadow/outline/emission`),
  - runtime forces `base` pass ON,
  - adds fallback reason `miq_pass_flags_defaulted_to_base`.

Expected impact:

- prevents black/empty frame outcomes (`drawcalls=0`, `active_passes=none`) caused by malformed export pass metadata.

### 2) Strict tracking wrapper script passthrough (`tools/release_readiness_strict_tracking.ps1`)

- Added wrapper switches:
  - `-SkipVersionContractCheck`
  - `-SkipQualityBaseline`
- Wrapper now forwards these switches to `release_readiness_gate.ps1`.

Expected impact:

- allows focused tracking-contract runs (MediaPipe/HostE2E/Fuzz) without forcing unrelated version/baseline blockers.

### 3) Tracking parser fuzz gate target alignment (`tools/tracking_parser_fuzz_gate/TrackingParserFuzzGate.csproj`)

- Target framework updated:
  - `net8.0-windows` -> `net8.0-windows10.0.19041`

Expected impact:

- aligns with host-side Windows TFM floor to reduce environment mismatch noise.

### 4) Tracking strict runtime runbook update

- `2026-03-06_tracking_strict_runtime_venv_runbook.md` command now includes:
  - `-SkipVersionContractCheck`
  - `-SkipQualityBaseline`
- PASS criteria line updated:
  - from `ReleaseCandidateWpfOnly: PASS`
  - to `TrackingContractCandidate: PASS`.

### 5) VRM probe/gate evidence refresh

- Updated tracked reports:
  - `build/reports/vrm_probe_fixed5.txt`
  - `build/reports/vrm_gate_fixed5.txt`
- Added richer per-sample MToon and warning diagnostics fields:
  - `MtoonOutlineMaterials`, `MtoonUvAnimMaterials`, `MtoonMatcapMaterials`,
  - `VrmSafeFallbackWarnings`, `VrmMatcapUnresolvedWarnings`, `VrmTextureUnresolvedWarnings`.
- Gate summary now includes:
  - `GateK (VRM MToon unresolved/fallback strict)`
  - `GateL (MToon advanced feature coverage)`.

## Verification

- `dotnet build NativeAnimiq/host/HostCore/HostCore.csproj -c Release --no-restore`: PASS
- `cmake --build NativeAnimiq/build --config Release --target nativecore`: PASS
- strict wrapper entry/argument forwarding verified through script diff and runbook update consistency.

## Notes

- This slice intentionally prioritizes runtime safety and diagnosability over aggressive auto-enable behavior.
- Generated VRM report timestamps changed as part of refreshed gate/probe execution evidence.
