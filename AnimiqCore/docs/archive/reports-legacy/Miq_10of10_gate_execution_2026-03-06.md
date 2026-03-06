# MIQ 10/10 Gate Hardening Execution Report (2026-03-06)

## Scope

This report captures the implementation and validation status of the MIQ "10/10" hardening baseline:

- warning-code normalization for quality gating
- stricter gate policy (`sample-count`, `warning-zero`, `manifest matching`)
- operational artifacts and current blocking factors

Out of scope:

- adding the full 10-sample corpus itself
- Unity-host parity work not directly involved in gate logic

## Implemented Changes

### 1) Warning-code normalization in native MIQ loader

Updated:

- `src/avatar/miq_loader.cpp`

Key behavior changes:

- `W_STAGE` is now treated as lifecycle telemetry only and excluded from `warning_codes[]`.
- Generic payload warnings without stable code token are excluded from `warning_codes[]`.
- Compatibility-note codes with `_PARTIAL` suffix are excluded from `warning_codes[]` while preserving `warnings[]` text.
- For `W_*` / `E_*` records, warning-code extraction now prefers the stable second token (e.g. `MIQ_*`/`XAV3_*`) when present.

Resulting intent:

- `warning_codes[]` now represents quality-impact signals suitable for strict gate decisions.

### 2) Regression gate strictness expansion

Updated:

- `tools/miq_render_regression_gate.ps1`

New/changed gate behavior:

- Default `-MinSampleCount` raised from `1` to `10` (`GateX0` stricter baseline).
- Added `-FailOnAnyWarnings` (`GateX6`) to enforce zero warning-code policy.
- Added sample-manifest contract checks:
  - `-SampleManifestPath`
  - `-FailOnManifestMismatch` (`GateX7`)
- Added `-RequireSnapshotParity` so snapshot parity can be mandatory even when paths are omitted.
- Summary/JSON rows expanded with:
  - sample class
  - expected primary error
  - expected max warning codes
  - explicit failure reason

### 3) 10-sample manifest template

Added:

- `tools/miq_gate_sample_manifest.example.json`

Template includes a 10-sample target layout:

- 6 normal
- 2 boundary
- 2 corrupt

with expected primary errors and warning-code thresholds.

### 4) Documentation sync

Updated:

- `docs/formats/miq.md`
- `CHANGELOG.md`

Format spec now documents warning-code accumulation exclusions (`W_STAGE`, generic non-coded payload notes, `_PARTIAL` compatibility notes).

## Verification Summary

Executed:

```powershell
cmake --build NativeAnimiq\build --config Release --target avatar_tool
NativeAnimiq\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.miq"
powershell -ExecutionPolicy Bypass -File NativeAnimiq\tools\miq_render_regression_gate.ps1 `
  -SampleDir D:\dbslxlvseefacedkfb `
  -AvatarToolPath D:\dbslxlvseefacedkfb\NativeAnimiq\build\Release\avatar_tool.exe `
  -FailOnRenderWarnings -FailOnAnyWarnings
```

Observed:

- `avatar_tool`: `WarningCodes: 0`, `CriticalWarningCount: 0` for current local sample.
- Gate outcomes:
  - `GateX1..X7`: PASS
  - `GateX0`: FAIL (current sample count is `1`, threshold is `10`)
  - Overall: FAIL (expected under strict 10-sample baseline)

Artifacts:

- `build/reports/miq_render_regression_gate_summary.txt`
- `build/reports/miq_render_regression_gate_summary.json`

## Known Risks or Limitations

- Current workspace still has only one `.miq` sample in gate scope; strict baseline cannot pass until sample corpus is expanded.
- Snapshot parity remains optional unless `-RequireSnapshotParity` is enabled with curated snapshot sets.
- Existing unrelated workspace changes are intentionally untouched by this slice.

## Next Steps

1. Populate and register at least 10 curated `.miq` samples (normal/boundary/corrupt) and enable `-FailOnManifestMismatch`.
2. Add snapshot reference sets and run with `-RequireSnapshotParity -FailOnSnapshotMismatch`.
3. Promote strict invocation as CI default for release branches.
