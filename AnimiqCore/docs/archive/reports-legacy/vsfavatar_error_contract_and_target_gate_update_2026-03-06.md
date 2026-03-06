# VSFAvatar error contract + target gate update (2026-03-06)

## Summary

This update finalizes the operator-facing diagnostics contract for `.vsfavatar` load failures and stabilizes sidecar probe output for gate consumption. It also expands fixed-set validation to explicitly include the real failure-class sample (`*11-3.vsfavatar`) in both render and quality gate tracks.

## Scope

- Runtime/sidecar contract stability for serialized candidate path output.
- Fixed-set gate behavior and parser rules for target-sample validation.
- Documentation sync for implementation and verification evidence.

## Implementation details

### 1) Sidecar JSON stability hardening

- File: `tools/vsfavatar_sidecar.cpp`
- Change:
  - `serialized_best_candidate_path` now emits `"NONE"` when the underlying value is empty.
  - This avoids blank-field ambiguity in gate parsing/required-field checks.
- Intent:
  - Preserve schema field presence and semantic meaning (`no candidate path`) without downstream parser special-casing.

### 2) Fixed-set target sample expansion

- Files:
  - `tools/vsfavatar_sample_report.ps1`
  - `tools/vsfavatar_quality_gate.ps1`
  - `tools/vsfavatar_render_gate.ps1`
- Change:
  - Added `*11-3.vsfavatar` to fixed sample inputs to include the real failure-class sample robustly across console/codepage differences.

### 3) Render gate target-sample contract

- File: `tools/vsfavatar_render_gate.ps1`
- Change:
  - Added `TargetSamplePattern` (default: `*11-3.vsfavatar`).
  - Added `GateR3` requiring that the target sample row is present in parsed report rows.
  - Summary now includes target metrics:
    - `target_stage`
    - `target_primary_error`
    - `target_mesh_payloads`
  - Tightened report parser to count only real sample blocks (not trailing summary/header-like lines).

### 4) Quality gate field rule alignment

- File: `tools/vsfavatar_quality_gate.ps1`
- Change:
  - `Require-Field` for `SidecarSerializedBestPath` now requires field presence, not non-empty value.
  - Rationale:
    - For some failures (`failed-serialized`), an empty path is valid diagnostic outcome.
    - With sidecar stabilization (`"NONE"`), field semantics remain explicit and gate-safe.

## Verification

Executed on 2026-03-06:

- `dotnet build .\host\HostCore\HostCore.csproj -c Release` -> PASS
- `cmake --build .\build --config Release --target nativecore avatar_tool vsfavatar_sidecar` -> PASS
- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_render_gate.ps1 -UseFixedSet` -> PASS
  - `SampleCount: 5`
  - `GateR1: PASS`
  - `GateR2: PASS`
  - `GateR3: PASS`
  - target: `stage=failed-serialized`, `primary=VSF_MESH_PAYLOAD_MISSING`, `mesh_payloads=0`
- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet` -> PASS
  - overall parser/host track: PASS
  - target sample appears as `NEW` with `primary=DATA_BLOCK_RANGE_FAILED` in diff view.

## Notes

- This update does not claim payload extraction success for the target sample yet.
- The immediate objective was to make failure diagnostics deterministic and visible in gate outputs while keeping parser-track quality gates green.
