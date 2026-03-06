# XAV2 Unity LTS Matrix Gate Policy Update (2026-03-06)

## Summary

Implemented matrix-driven Unity LTS gate orchestration for XAV2 with
official-line blocking policy and release dashboard integration.

## Implemented Changes

- `tools/unity_xav2_lts_gate.ps1` (new/updated)
  - default official lines: `2021-lts`, `2022-lts`, `2023-lts`
  - default candidate lines: empty
  - enforces policy:
    - official lines must all pass (`official-lines-all-pass-required`)
- `tools/xav2_parity_gate.ps1`
  - migrated to Unity line + matrix-aware execution (`UnityLine`, `MatrixPath`)
  - supports per-line artifact suffix while preserving legacy 2021 summary copies
- orchestrates per-line execution of:
    - `unity_xav2_validate.ps1`
    - `xav2_parity_gate.ps1`
    - `xav2_compression_quality_gate.ps1`
- `tools/unity_xav2_env_bootstrap.ps1`
  - upgraded from fixed env var setup to Unity line/matrix-aware bootstrap
  - resolves and sets editor env var based on `unity_lts_matrix.json`
- `tools/run_quality_baseline.ps1`
  - `-EnableUnityXav2LtsGate` default set to enabled
- `tools/release_readiness_gate.ps1`
  - `-EnableUnityXav2LtsGate` default set to enabled and passed through to quality baseline
- `tools/release_gate_dashboard.ps1`
  - added `Unity XAV2 LTS Gate` row
  - release decision now hard-fails Unity XAV2 status when LTS gate is `FAIL`
  - fallback to legacy single-line signals only when LTS gate is missing/unknown

## Documentation Updates

- `unity/Packages/com.vsfclone.xav2/README.md`
  - support matrix aligned to unified official model:
    - official: 2021-lts, 2022-lts, 2023-lts
- `docs/public/compatibility.md`
  - aligned unified official support and matrix orchestration references
- `docs/public/migration.md`
  - aligned migration text to unified official model

## Validation

- PowerShell parser check PASS for updated scripts:
  - `xav2_parity_gate.ps1`
  - `unity_xav2_lts_gate.ps1`
  - `unity_xav2_env_bootstrap.ps1`
  - `run_quality_baseline.ps1`
  - `release_readiness_gate.ps1`
  - `release_gate_dashboard.ps1`
