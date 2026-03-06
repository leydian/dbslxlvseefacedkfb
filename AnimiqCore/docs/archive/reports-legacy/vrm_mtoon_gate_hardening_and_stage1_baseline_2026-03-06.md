# VRM MToon Gate Hardening + Stage1 Baseline (2026-03-06)

## Summary

This pass establishes stage-1 operational baseline for gradual MToon parity rollout:

- normalize VRM unresolved material warning codes in runtime,
- expose MToon slot coverage and VRM fallback/unresolved counters in `avatar_tool`,
- extend VRM quality gate with explicit MToon checks (`GateK`, `GateL`).

Goal is to make MToon regressions machine-detectable before feature-expansion steps.

## Implementation

### 1) Warning-code normalization for VRM path

File: `src/nativecore/native_core.cpp`

- Unresolved texture warnings are now source-aware:
  - VRM: `VRM_MATERIAL_TEXTURE_UNRESOLVED`
  - MIQ: `MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED` (kept)
- Classifier path updated so VRM material/mtoon warnings are categorized as render warnings:
  - `VRM_MATERIAL_TEXTURE_UNRESOLVED`
  - `VRM_MATERIAL_SAFE_FALLBACK_APPLIED`
  - `VRM_MTOON_MATCAP_UNRESOLVED`

Result:
- VRM and MIQ diagnostics are no longer mixed under a single MIQ code family.

### 2) Probe output expansion for MToon stage gating

File: `tools/avatar_tool.cpp`

- Added per-run counters:
  - `MtoonOutlineMaterials`
  - `MtoonUvAnimMaterials`
  - `MtoonMatcapMaterials`
  - `VrmSafeFallbackWarnings`
  - `VrmMatcapUnresolvedWarnings`
  - `VrmTextureUnresolvedWarnings`
- Warning classification in the tool aligned with nativecore additions.

Result:
- Quality gate can evaluate MToon coverage and unresolved/fallback conditions directly.

### 3) VRM quality gate extension

File: `tools/vrm_quality_gate.ps1`

- Added parse/load for new probe fields.
- Added `GateK` (strict):
  - fail when any sample has unresolved/fallback VRM MToon warnings.
- Added `GateL` (coverage):
  - reports advanced-feature coverage mode (`full/partial/no-advanced-feature-coverage/...`).
  - configured as non-breaking PASS with mode signal for current fixed5 cohort.
- Per-sample summary now includes:
  - `mtoon(adv/fallback/outline/uv/matcap)=...`
  - `vrmWarn(safe/matcap/tex)=...`

Result:
- Stage-1 has hard failure signal for unresolved/fallback regressions and observability signal for coverage progress.

## Verification

- Build:
  - `cmake --build NativeAnimiq/build-thumb --config Release --target nativecore avatar_tool`
  - result: PASS
- Probe smoke:
  - `avatar_tool sample/개인작11-3.vrm`
  - result: PASS
  - new fields emitted as expected.
- Gate:
  - `powershell -ExecutionPolicy Bypass -File tools/vrm_quality_gate.ps1 -SampleDir sample -AvatarToolPath build-thumb/Release/avatar_tool.exe -Profile fixed5`
  - result: PASS
  - Gate status:
    - A..J PASS
    - K PASS
    - L PASS (`mode=no-advanced-feature-coverage`)

## Notes

- `GateL` is intentionally observational in this stage to preserve baseline stability while feature-coverage cohorts are expanded.
- Next stage should introduce dedicated sample cohort that guarantees outline/uv/matcap presence, then tighten `GateL` policy.
