# VSFAvatar sidecar v5 contract + gate wiring (2026-03-06)

## Summary

Implemented a VSFAvatar diagnostics contract upgrade focused on deterministic failure classification and gate-visible telemetry for the sidecar-first load path.

This change does **not** complete authored mesh payload extraction yet. It upgrades schema/loader/gates so extraction progress and failure classes are explicit and testable.

## Scope

- Sidecar JSON contract upgrade (`v4 -> v5`)
- Native loader schema validation + diagnostics sync
- Report/gate script wiring for new contract fields
- Smoke verification for parser track and contract presence

## Implementation details

### 1) Sidecar JSON schema upgrade (`schema_version=5`)

- File: `tools/vsfavatar_sidecar.cpp`
- Changes:
  - schema/version bump:
    - `schema_version: 5`
    - `extractor_version: inhouse-sidecar-v5`
  - added fields:
    - `recovery_attempt_profile`
    - `mesh_extract_stage`
    - `timing_ms`
  - added staged primary-error normalization when probe primary is `NONE`:
    - `VSF_MESH_EXTRACT_FAILED`
    - `VSF_SERIALIZED_TABLE_INCOMPLETE`
    - `VSF_MESH_PAYLOAD_MISSING`
  - added sidecar runtime timing capture (steady clock, milliseconds).

### 2) Loader schema/diagnostics synchronization

- File: `src/avatar/vsfavatar_loader.cpp`
- Changes:
  - `ValidateSidecarSchema(...)` now accepts schema `2|3|4|5`.
  - for `schema_version >= 5`, requires:
    - `recovery_attempt_profile`
    - `mesh_extract_stage`
    - `timing_ms`
  - consumes new fields and emits warnings:
    - `W_RECOVERY_PROFILE: ...`
    - `W_MESH_EXTRACT_STAGE: ...`
    - `W_TIMING_MS: ...`
  - refined no-mesh fallback primary-error assignment (instead of unconditional `VSF_MESH_PAYLOAD_MISSING`):
    - `VSF_MESH_EXTRACT_FAILED` if mesh objects are discovered but payload remains missing
    - `VSF_SERIALIZED_TABLE_INCOMPLETE` if parser stage is `failed-serialized`
    - `VSF_MESH_PAYLOAD_MISSING` otherwise

### 3) Sample report propagation

- File: `tools/vsfavatar_sample_report.ps1`
- Added emitted fields:
  - `SidecarRecoveryAttemptProfile`
  - `SidecarMeshExtractStage`
  - `SidecarTimingMs`

### 4) Quality gate expansion

- File: `tools/vsfavatar_quality_gate.ps1`
- Added required fields:
  - `SidecarRecoveryAttemptProfile`
  - `SidecarMeshExtractStage`
  - `SidecarTimingMs`
- Added aggregate metrics:
  - sidecar timing avg/max
  - mesh extract stage distribution

### 5) Render gate expansion

- File: `tools/vsfavatar_render_gate.ps1`
- Added row parsing for:
  - `SidecarMeshExtractStage`
  - `SidecarTimingMs`
- Added `GateR4`:
  - target sample row must contain sidecar v5 contract fields
- summary now prints:
  - `target_mesh_extract_stage`
  - `target_timing_ms`

## Verification (executed on 2026-03-06)

- Build:
  - `cmake --build .\build --config Release --target vsfavatar_sidecar` -> PASS
  - full target build including `avatar_tool` hit local file lock:
    - `LNK1104: cannot open ...\avatar_tool.exe`
- Sidecar contract check:
  - `build\Release\vsfavatar_sidecar.exe <sample.vsfavatar>` -> PASS
  - confirmed output includes:
    - `schema_version=5`
    - `recovery_attempt_profile`
    - `mesh_extract_stage`
    - `timing_ms`
- Quality gate smoke:
  - `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseSmoke -SmokeMaxFiles 1` -> PASS
  - parser track passed under smoke gating policy.
- Render gate reduced target:
  - `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_render_gate.ps1 -UseFixedSet -FixedSamples "Character vywjd.vsfavatar" -TargetSamplePattern "Character vywjd.vsfavatar"`
  - contract gate result:
    - `GateR4: PASS` (new contract fields parsed)
  - overall remained FAIL due existing payload condition:
    - `GateR1: FAIL` (`MeshPayloads=0`)

## Current limitation

- The pipeline still relies on placeholder/no-authored-mesh path for this sample class.
- This update establishes explicit diagnostics and gate contracts needed before full authored mesh extraction can be completed.
