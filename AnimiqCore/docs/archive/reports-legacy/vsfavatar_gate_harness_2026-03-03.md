# VSFAvatar Quality Gate Harness (2026-03-03)

## Summary

This report documents the gate harness added for fixed-set VSFAvatar regression checks.
The harness standardizes pass/fail evaluation for parser iterations and provides baseline diff output.

## Scope

- Added `tools/vsfavatar_quality_gate.ps1`
- Kept `tools/vsfavatar_sample_report.ps1` as the primary probe producer
- Added `GateInputVersion: 1` header marker for parser stability

## Gate Definitions

- Gate A: all samples must include required sidecar fields and complete without parse/process failure
- Gate B: at least one sample must reach `failed-serialized` or `complete`
- Gate C: when `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`, tuple evidence must exist:
  - `SidecarFailedReadOffset > 0`
  - `SidecarFailedCompressedSize > 0`
  - `SidecarFailedUncompressedSize > 0`
  - `SidecarOffsetFamily` must be non-empty

Gate B is strict-fail by default.

## Diff Labels

Per-sample baseline compare emits one of:

- `IMPROVED`
- `REGRESSED`
- `CHANGED`
- `UNCHANGED`
- `NEW`

Current comparison keys:

- `SidecarProbeStage`
- `SidecarPrimaryError`
- `SidecarOffsetFamily`
- `SidecarReconCandidateCount`
- `SidecarBestCandidateScore`
- `SidecarFailedReadOffset`
- `SidecarFailedCompressedSize`
- `SidecarFailedUncompressedSize`

## Outputs

- Probe report: `build/reports/vsfavatar_probe_latest_after_gate.txt`
- Gate summary: `build/reports/vsfavatar_gate_summary.txt`
- Baseline input (default): `build/reports/vsfavatar_probe_fixed.txt`

## Exit Code Contract

- `0`: all gates pass
- `1`: one or more gates fail

## Execution

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
```

## Latest Fixed-set Result

- Gate A: `PASS`
- Gate B: `FAIL`
- Gate C: `PASS`
- Overall: `FAIL` (strict Gate B policy)

Diff aggregate from baseline compare:

- `Improved=0`
- `Regressed=0`
- `Changed=4`
- `Unchanged=0`
- `New=0`

## Notes

The harness is intentionally contract-focused and does not mutate parser behavior.
It is designed for parallel use while decode logic evolves in other sessions.
