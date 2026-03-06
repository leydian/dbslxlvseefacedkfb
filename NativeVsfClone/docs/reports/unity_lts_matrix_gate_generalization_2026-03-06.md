# Unity LTS Matrix Expansion + Gate Generalization (2026-03-06)

## Scope

- Expand Unity compatibility support from a single pinned baseline (`2021.3.18f1`) to a matrix-driven LTS contract.
- Generalize Unity validation/quality/parity gate scripts for multi-line execution.
- Update CI workflow and public package/docs contract to reflect official support lines.
- Preserve backward compatibility for existing 2021-only report consumers.

## Implemented Changes

### 1) LTS matrix source of truth

- Added `tools/unity_lts_matrix.json` with line metadata:
  - `2021-lts` -> `2021.3.18f1`, env `UNITY_2021_3_18F1_EDITOR_PATH`
  - `2022-lts` -> `2022.3.62f1`, env `UNITY_2022_3_LTS_EDITOR_PATH`
  - `2023-lts` -> `2023.2.20f1`, env `UNITY_2023_2_LTS_EDITOR_PATH`

This removes repeated version/env hardcoding spread across scripts and workflow.

### 2) Gate script generalization (matrix-aware)

Updated scripts to accept multi-line parameters and resolve defaults from matrix:

- `tools/unity_xav2_validate.ps1`
- `tools/xav2_compression_quality_gate.ps1`
- `tools/xav2_parity_gate.ps1`
- `tools/unity_xav2_env_bootstrap.ps1`

Common behavior additions:

- New input surface:
  - `-UnityLine` (default `2021-lts`)
  - `-MatrixPath` (default `./tools/unity_lts_matrix.json`)
  - `-ExpectedUnityVersion` optional override
  - `-ReportSuffix` for line-scoped output names
- Automatic editor path resolution from per-line env var in matrix.
- Line-suffixed report/artifact filenames for parallel multi-line execution.

Compatibility safeguards for existing automation:

- For `2021-lts`, scripts also emit legacy unsuffixed report filenames via copy.
- This keeps existing release/dashboard tooling functional while new matrix outputs coexist.

### 3) CI workflow matrix conversion

- Updated `.github/workflows/unity-xav2-compat.yml`:
  - workflow name changed to `unity-xav2-lts-compat`
  - single fixed job replaced with matrix job over `2021-lts`, `2022-lts`, `2023-lts`
  - each matrix row injects expected version and editor env-var key
  - artifacts uploaded per line (`unity-xav2-<line>-compat-reports`)
  - path filters include `tools/unity_lts_matrix.json`

### 4) Public compatibility contract/documentation updates

Updated docs to align with matrix-based official support model:

- `docs/public/compatibility.md`
- `docs/public/migration.md`
- `unity/Packages/com.vsfclone.xav2/README.md`
- `unity/Packages/com.vsfclone.xav2/package.json`

Contract updates include:

- Official Unity support lines listed as `2021/2022/2023` LTS pins.
- Minimum package floor remains `2021.3` + `unityRelease: 18f1`.
- Gate-backed official support statement preserved.
- Runtime policy docs remain consistent with current defaults (`ShaderPolicy = WarnFallback`, strict mode available).

## Verification Summary

- Performed static verification:
  - Parsed all modified PowerShell scripts successfully (`ScriptBlock::Create` parse pass).
  - Reviewed diffs for matrix parameter plumbing, artifact naming, and CI matrix wiring.
- Not executed in this change set:
  - Live Unity editor gate runs for each line (environment-dependent).

## Known Risks or Limitations

- New CI matrix requires all referenced editor path variables to be configured on runners:
  - `UNITY_2021_3_18F1_EDITOR_PATH`
  - `UNITY_2022_3_LTS_EDITOR_PATH`
  - `UNITY_2023_2_LTS_EDITOR_PATH`
- If any line's editor/runtime dependencies differ materially, line-specific failures can occur until environment parity is established.
- Legacy unsuffixed report compatibility is intentionally limited to `2021-lts` path.

## Next Steps

1. Configure all Unity editor env vars on self-hosted runners and validate path accessibility.
2. Execute matrix CI once and confirm all three lines produce artifacts and pass/fail independently.
3. If needed, add a release summary script that aggregates per-line suffixed reports into one rollout dashboard.
