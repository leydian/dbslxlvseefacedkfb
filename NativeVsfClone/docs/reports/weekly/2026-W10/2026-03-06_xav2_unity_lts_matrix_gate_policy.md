# XAV2 Unity LTS Matrix Gate Policy Update (2026-03-06)

## Summary

Implemented matrix-driven Unity LTS gate orchestration for XAV2 with
official-line blocking policy and release dashboard integration.

## Implemented Changes

- `tools/xav2_parity_gate.ps1`
  - migrated to Unity line + matrix-aware execution (`UnityLine`, `MatrixPath`)
  - supports per-line artifact suffix while preserving legacy 2021 summary copies
- `tools/unity_xav2_lts_gate.ps1` (new)
  - orchestrates per-line execution of:
    - `unity_xav2_validate.ps1`
    - `xav2_parity_gate.ps1`
    - `xav2_compression_quality_gate.ps1`
  - enforces policy:
    - official lines must all pass (`official-lines-all-pass-required`)
    - candidate lines may be included optionally
- `tools/unity_xav2_env_bootstrap.ps1`
  - upgraded from fixed env var setup to Unity line/matrix-aware bootstrap
  - resolves and sets editor env var based on `unity_lts_matrix.json`
- `tools/run_quality_baseline.ps1`
  - added optional `-EnableUnityXav2LtsGate`
- `tools/release_readiness_gate.ps1`
  - added optional `-EnableUnityXav2LtsGate` passthrough to quality baseline
- `tools/release_gate_dashboard.ps1`
  - added `Unity XAV2 LTS Gate` row
  - release decision now prefers matrix-gate signal when available

## Documentation Updates

- `unity/Packages/com.vsfclone.xav2/README.md`
  - support matrix now expresses phase model:
    - official (phase 1): 2021-lts, 2022-lts
    - candidate (phase 2 target): 2023-lts
- `docs/public/compatibility.md`
  - aligned official/candidate split and matrix orchestration references
- `docs/public/migration.md`
  - aligned migration text to phase model

## Validation

- PowerShell parser check PASS for updated scripts:
  - `xav2_parity_gate.ps1`
  - `unity_xav2_lts_gate.ps1`
  - `unity_xav2_env_bootstrap.ps1`
  - `run_quality_baseline.ps1`
  - `release_readiness_gate.ps1`
  - `release_gate_dashboard.ps1`
