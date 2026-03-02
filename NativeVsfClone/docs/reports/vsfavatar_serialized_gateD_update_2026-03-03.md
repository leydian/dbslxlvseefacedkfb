# VSFAvatar Serialized Candidate Expansion + GateD Update (2026-03-03)

## Summary

This update focused on the next VSFAvatar compatibility milestone: improving serialized candidate probing quality and tightening quality-gate strictness toward at least one fixed sample reaching `complete`.

Scope of this pass:

- expanded serialized-node parse attempts with bounded offset deltas
- added serialized-candidate diagnostics through probe -> sidecar -> loader -> report flow
- hardened fixed-set report integrity checks
- added strict Gate D to enforce the first `complete + object_table_parsed + no primary error` milestone

Current outcome:

- Gate A: PASS
- Gate B: PASS
- Gate C: PASS
- Gate D: FAIL
- Overall: FAIL

Gate D remains the active blocker.

## Goals and Decisions

- Keep runtime strategy unchanged:
  - default remains sidecar-first (`VSF_PARSER_MODE=sidecar`)
  - in-house parser remains fallback/diagnostic path
- Treat fixed-set report completeness as a hard requirement.
- Keep diagnostics backward-compatible by adding optional fields rather than changing required schema fields.

## Implementation Details

### 1) UnityFS probe observability extensions

File:

- `include/vsfclone/vsf/unityfs_reader.h`

Added fields:

- `serialized_attempt_count`
- `serialized_best_candidate_path`
- `serialized_best_candidate_score`

Purpose:

- expose actual serialized parse effort and best-candidate selection details for triage and gate automation.

### 2) Serialized candidate probing expansion

File:

- `src/vsf/unityfs_reader.cpp`

Changes:

- For each serialized node candidate, parsing now tries bounded offset deltas:
  - `0`, `+16`, `-16`, `+32`, `-32`, `+64`, `-64`
- Added duplicate-range guard to avoid retrying identical windows.
- Added candidate scoring:
  - parsed object count dominance
  - major-type diversity tie-break (`GameObject`, `Mesh`, `Material`, `Texture2D`, `SkinnedMeshRenderer`)
- On all-fail cases, preserves best-scored failure context and candidate path.

Effect:

- more robust serialized probing without changing reconstruction flow or parser-mode policy.

### 3) Serialized file parse error normalization

File:

- `src/vsf/serialized_file_reader.cpp`

Changes:

- Added parse-error classifier and embedded both-endian classification into final failure string:
  - `SF_PARSE_BOTH_ENDIAN_FAILED[<little_code>|<big_code>]`
- Normalized successful parse summary error code to `NONE`.

Effect:

- improves failure interpretability in logs/reports and preserves existing return contract.

### 4) Sidecar/loader diagnostics contract extension

Files:

- `tools/vsfavatar_sidecar.cpp`
- `src/avatar/vsfavatar_loader.cpp`

Changes:

- Sidecar JSON adds optional serialized diagnostics:
  - `serialized_candidate_count`
  - `serialized_attempt_count`
  - `serialized_best_candidate_path`
  - `serialized_best_candidate_score`
- Loader now maps these into warning streams.
- In-house path warning metadata includes serialized candidate/attempt/best selection fields.

Effect:

- probe observability is now end-to-end visible in both sidecar and fallback paths.

### 5) Fixed-set report hardening + Gate D

Files:

- `tools/vsfavatar_sample_report.ps1`
- `tools/vsfavatar_quality_gate.ps1`

Sample report changes:

- Fixed-set mode now fails if any expected fixed sample is missing.
- Sidecar result must be `status=ok`.
- Added output fields:
  - `SidecarObjectTableParsed`
  - `SidecarSerializedAttempts`
  - `SidecarSerializedBestPath`
  - `SidecarSerializedBestScore`
- Added strict integrity check:
  - `GateRows` must equal processed file count.
- Added gate line:
  - `GateD_AtLeastOneCompleteWithObjectTable`

Gate script changes:

- Added Gate D:
  - at least one sample must satisfy:
    - `SidecarProbeStage=complete`
    - `SidecarObjectTableParsed=True`
    - `SidecarPrimaryError` is `NONE` or empty
- Added Gate A integrity checks for header/sample alignment (`FileCount`, `GateRows`).
- Overall pass now requires:
  - `GateA && GateB && GateC && GateD`

## Documentation Updates

Files:

- `README.md`
- `CHANGELOG.md`

Updates include:

- Gate D rule and strict-fail behavior
- sidecar serialized diagnostics contract
- latest behavior notes for this pass
- detailed changelog entry for implementation/verification scope

## Verification

Build:

- `cmake --build build --config Release --target vsfavatar_sidecar avatar_tool` succeeded.

Gate run:

- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`

Observed result:

- GateA=PASS
- GateB=PASS
- GateC=PASS
- GateD=FAIL
- Overall=FAIL

Summary source:

- `build/reports/vsfavatar_gate_summary.txt`

Fixed-set report regeneration:

- re-generated `build/reports/vsfavatar_probe_latest_after_scoring.txt` to restore full 4-sample output + gate block consistency.

## Current Blocker

- No fixed sample has reached:
  - `probe_stage=complete`
  - `object_table_parsed=true`
  - `primary_error_code=NONE|empty`

This remains the single blocker for Gate D and for the “first complete sample” milestone.

