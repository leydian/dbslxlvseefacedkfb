# VXAvatar/VXA2 Quality Gate Harness (2026-03-02)

## Summary

This report documents the quality gate harness for `.vxavatar` and `.vxa2` regression checks.
The harness standardizes parser-output checks from `avatar_tool` and includes synthetic corruption inputs for stability testing.

## Scope

- Added `tools/vxavatar_sample_report.ps1`
- Added `tools/vxavatar_quality_gate.ps1`
- Added `GateInputVersion: 1` marker for report parser stability

## Synthetic Sample Policy

Synthetic files are regenerated under `build/tmp_vx/` from fixed baseline samples:

- `demo_mvp_truncated.vxavatar`: truncated ZIP payload
- `demo_mvp_cd_mismatch.vxavatar`: local-header compressed-size mismatch mutation
- `demo_tlv_truncated.vxa2`: truncated TLV section stream

## Gate Definitions

- Gate A: fixed `.vxavatar` contract
  - `Format=VXAvatar`
  - `Compat=full`
  - `ParserStage=runtime-ready`
  - `PrimaryError=NONE`
  - `MeshPayloads >= 1`, `TexturePayloads >= 1`
- Gate B: synthetic corrupted `.vxavatar` contract
  - `Compat=failed|partial`
  - `PrimaryError=VX_SCHEMA_INVALID|VX_UNSUPPORTED_COMPRESSION`
  - process must exit normally (no crash/timeout)
- Gate C: `.vxa2` fixed + corrupted contract
  - fixed sample: `Format=VXA2` and parser stage in `parse|resolve|payload|runtime-ready`
  - corrupted sample: `PrimaryError=VXA2_SECTION_TRUNCATED|VXA2_SCHEMA_INVALID`
- Gate D: output contract
  - required fields in each sample block:
    - `InputKind`
    - `InputTag`
    - `Format`
    - `Compat`
    - `ParserStage`
    - `PrimaryError`

## Outputs

- Probe report: `build/reports/vxavatar_probe_latest.txt`
- Gate summary: `build/reports/vxavatar_gate_summary.txt`

## Exit Code Contract

- `0`: all gates pass
- `1`: one or more gates fail

## Execution

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet
```

## Latest Fixed-set Result (2026-03-03)

- Gate A: `PASS`
- Gate B: `PASS`
- Gate C: `PASS`
- Gate D: `PASS`
- Overall: `PASS`

Sample coverage:

- `FixedVX=1`
- `CorruptVX=2`
- `FixedVXA2=1`
- `CorruptVXA2=1`

Representative per-sample outcomes:

- `demo_mvp.vxavatar`
  - `Format=VXAvatar`, `Compat=full`, `ParserStage=runtime-ready`, `PrimaryError=NONE`
- `demo_mvp_truncated.vxavatar`
  - `Format=VXAvatar`, `Compat=failed`, `PrimaryError=VX_SCHEMA_INVALID`
- `demo_mvp_cd_mismatch.vxavatar`
  - `Format=VXAvatar`, `Compat=failed`, `PrimaryError=VX_SCHEMA_INVALID`
- `demo_mvp.vxa2`
  - `Format=VXA2`, `Compat=partial`, `ParserStage=runtime-ready`, `PrimaryError=VXA2_ASSET_MISSING`
- `demo_tlv_truncated.vxa2`
  - `Format=VXA2`, `Compat=failed`, `PrimaryError=VXA2_SCHEMA_INVALID`

## Notes

This harness is intentionally CLI-output contract focused.
It does not replace future unit/integration tests and is intended as a deterministic regression safety net while parser behavior evolves.

Profile/CI expansion note:

- As of 2026-03-03, profile-based gate operation (`quick`/`full`) and CI wiring are documented in:
  - `docs/reports/vxavatar_gate_ci_expansion_2026-03-03.md`
