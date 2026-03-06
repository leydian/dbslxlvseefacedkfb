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
- Gate D: PASS
- Overall: PASS

Gate D blocker is resolved for the fixed sample set.

## DoD Status

- [x] Parser Track DoD (`GateA && GateB && GateC && GateD`)
- [ ] Host Track DoD (`HostTrackStatus=READY` and WinUI parity runtime validation complete)

## Goals and Decisions

- Keep runtime strategy unchanged:
  - default remains sidecar-first (`VSF_PARSER_MODE=sidecar`)
  - in-house parser remains fallback/diagnostic path
- Treat fixed-set report completeness as a hard requirement.
- Keep diagnostics backward-compatible by adding optional fields rather than changing required schema fields.

## Implementation Details

### 1) UnityFS probe observability extensions

File:

- `include/animiq/vsf/unityfs_reader.h`

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
- GateD=PASS
- Overall=PASS

Summary source:

- `build/reports/vsfavatar_gate_summary.txt`

Fixed-set and aggregate outputs:

- `build/reports/vsfavatar_probe_latest_after_gate.txt`
- `build/reports/vsfavatar_gate_summary.txt`
- `build/reports/vsfavatar_gate_aggregate.csv`
- `build/reports/vsfavatar_gate_aggregate.txt`

## Current Status

- Parser track milestones for this pass are complete.
- Remaining open item is host-track runtime parity after WinUI XAML compiler unblocking.

## Final Implementation Snapshot (2026-03-03)

### Added operational mode split

- `tools/vsfavatar_quality_gate.ps1`
  - added smoke mode for fast loops:
    - `-UseSmoke`
    - `-SmokeMaxFiles <N>`
  - mode-based gate blocking:
    - smoke: `GateA && GateB && GateC` required
    - fixed/default: `GateA && GateB && GateC && GateD` required
  - parser/host track summary lines:
    - `ParserTrack_DoD`
    - `HostTrack_DoD`
    - `HostTrackStatus`
  - aggregate artifacts:
    - `build/reports/vsfavatar_gate_aggregate.csv`
    - `build/reports/vsfavatar_gate_aggregate.txt`

- `tools/vsfavatar_sample_report.ps1`
  - added output fields:
    - `HostTrackStatus`
    - `ParserTrack_DoD`
    - `HostTrack_DoD`
    - `RunDurationSec`

### Parse and report robustness improvements

- report parser now guards against false sample-header detection in tool output body:
  - sample section detection constrained by:
    - `FileCount` upper bound
    - header pattern `---- <name>.vsfavatar`
  - accepts both shapes after a sample header:
    - `Load ...`
    - `Sidecar...`-first output

- baseline handling:
  - missing baseline no longer hard-fails the gate script
  - warning emitted and diff baseline treated as empty

### Verification replay (actual run outputs)

- smoke run:
  - command:
    - `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseSmoke -SmokeMaxFiles 1`
  - observed:
    - `GateA=PASS`
    - `GateB=PASS`
    - `GateC=PASS`
    - `GateD=PASS` (reported, non-blocking in smoke mode)
    - `Overall=PASS`
    - `RunDurationSec=167.817`
    - `SerializedAttempts_Avg=148`
    - `SerializedAttempts_Max=148`

- fixed-set run:
  - command:
    - `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
  - observed:
    - `GateA=PASS`
    - `GateB=PASS`
    - `GateC=PASS`
    - `GateD=PASS`
    - `Overall=PASS`
    - `RunDurationSec=421.558`
    - `SerializedAttempts_Avg=126`
    - `SerializedAttempts_Max=148`
    - `ObjectTableParsed_True=4`
    - `ObjectTableParsed_False=0`

### Files updated in this pass

- `src/vsf/serialized_file_reader.cpp`
- `src/vsf/unityfs_reader.cpp`
- `tools/vsfavatar_sample_report.ps1`
- `tools/vsfavatar_quality_gate.ps1`
- `README.md`
- `CHANGELOG.md`
- `docs/reports/vsfavatar_serialized_gateD_update_2026-03-03.md`
