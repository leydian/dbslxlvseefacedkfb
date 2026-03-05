# XAV2 Warning Contract + Regression Gate Upgrade (2026-03-06)

## Scope

This report summarizes the latest XAV2 quality-hardening slice focused on:

- warning-code contract clarity (severity/category/critical intent)
- regression gate decision quality and machine-readable output
- documentation sync for release/operator use

Out of scope:

- broad host/runtime feature work outside XAV2 warning/gate contracts
- Unity package behavior changes not tied to warning-code interpretation

## Implemented Changes

### 1) XAV2 regression gate hardening

Updated file:

- `tools/xav2_render_regression_gate.ps1`

Behavior changes:

- Added minimum sample-count gate:
  - `GateX0` with `-MinSampleCount` parameter
  - default set to `1` for backward compatibility in current workspace
- Added warning metadata parsing:
  - consumes `WarningCodeMeta[n]` lines when present
  - keeps fallback logic for older `avatar_tool` output
- GateX4 logic shifted from ad-hoc list scanning to structured critical-warning evaluation:
  - per-row `CriticalWarningCount`
  - strict fail when critical warnings are present (`-FailOnRenderWarnings`)
- Added per-row failure diagnostics:
  - `failure_reason` values:
    - `none`
    - `critical-warning-present`
    - `parser-stage-not-runtime-ready`
    - `primary-error-not-none`
- Expanded summary row fields:
  - severity counts (`info|warn|error`)
  - critical count
  - last warning code/severity/category
  - failure reason
- Added JSON artifact output:
  - `-JsonSummaryPath`
  - includes gate booleans and row-level structured diagnostics

### 2) XAV2 warning-code contract documentation sync

Updated file:

- `docs/formats/xav2.md`

Added explicit warning-code contract section:

- severity/category mapping rules
- critical render warning-code list used by gate policy:
  - `XAV2_SKINNING_STATIC_DISABLED`
  - `XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED`
  - `XAV3_SKELETON_PAYLOAD_MISSING`
  - `XAV3_SKELETON_MESH_BIND_MISMATCH`
  - `XAV3_SKINNING_MATRIX_INVALID`
  - `XAV2_UNKNOWN_SECTION_NOT_ALLOWED`

## Verification Summary

Executed commands:

```powershell
cmake --build NativeVsfClone\build --config Release --target avatar_tool
cmake --build NativeVsfClone\build --config Release --target nativecore
NativeVsfClone\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.xav2"
powershell -ExecutionPolicy Bypass -File NativeVsfClone\tools\xav2_render_regression_gate.ps1 `
  -SampleDir D:\dbslxlvseefacedkfb `
  -AvatarToolPath D:\dbslxlvseefacedkfb\NativeVsfClone\build\Release\avatar_tool.exe `
  -SummaryPath D:\dbslxlvseefacedkfb\build\reports\xav2_render_regression_gate_summary.txt `
  -JsonSummaryPath D:\dbslxlvseefacedkfb\build\reports\xav2_render_regression_gate_summary.json `
  -FailOnRenderWarnings
```

Results:

- `avatar_tool` build: PASS
- `nativecore` build: PASS
- regression gate: PASS
  - GateX0..GateX5 all PASS in current sample set
- artifacts generated:
  - `build/reports/xav2_render_regression_gate_summary.txt`
  - `build/reports/xav2_render_regression_gate_summary.json`

Observed sample-level output (current workspace sample):

- `warning_codes=W_STAGE,W_STAGE,W_PAYLOAD,W_STAGE,W_STAGE`
- `severity_counts=info:4|warn:1|error:0`
- `critical=0`
- `failure_reason=none`

## Known Risks or Limitations

- Current local run has only one `.xav2` sample in gate scope; stronger release confidence requires higher `-MinSampleCount` and larger representative sets.
- This slice improves warning signal quality and gate interpretation, but does not by itself remove non-critical warnings from payload generation.
- Host .NET build validation remains environment-dependent where NuGet network access is blocked.

## Next Steps

1. Raise CI default to `-MinSampleCount 3` (or higher) once curated sample set is in place.
2. Ingest JSON gate artifact into release dashboard for trend analysis.
3. Add explicit allowlist/denylist policy file for warning-code governance across Native/Unity/Host pipelines.
