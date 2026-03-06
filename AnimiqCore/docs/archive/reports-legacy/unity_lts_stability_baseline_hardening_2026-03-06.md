# Unity LTS Stability Baseline Hardening Summary (2026-03-06)

## Scope

This document summarizes the Unity LTS stability-baseline hardening work that upgraded the project from single-line compatibility assumptions to evidence-driven multi-line governance.

Primary focus:

- harden policy and automation around Unity LTS support lines
- make release decisions depend on structured LTS gate evidence
- improve observability (line-level status + trend-ready artifacts)

Out of scope:

- URP/HDRP support expansion
- non-Windows parity guarantees
- shader-family/rendering feature surface expansion

## Implemented Changes

### 1) Official LTS policy promoted to 3 lines

`tools/unity_miq_lts_gate.ps1` now treats all three lines as official by default:

- `2021-lts`
- `2022-lts`
- `2023-lts`

Policy remains explicit: `official-lines-all-pass-required`.

### 2) Preflight safety checks before gate execution

The LTS gate script now performs line-by-line preflight validation before running Unity jobs:

- matrix entry existence per line
- expected Unity version and editor env-var mapping validation
- editor executable existence check
- Unity project path existence check

If preflight fails for any official line, summary is emitted and the gate fails early with actionable context.

### 3) Structured per-line status aggregation

In addition to raw step results, line-level aggregate status is now computed and recorded:

- validate
- parity
- compression
- line overall (PASS/FAIL)

This makes failure localization deterministic and removes ambiguity from release decisions.

### 4) Evidence persistence for trend analysis

The LTS gate now writes longitudinal artifacts:

- `build/reports/unity_miq_lts_gate_history.csv`
- `build/reports/unity_miq_lts_kpi_summary.json`
- `build/reports/unity_miq_lts_kpi_summary.txt`

KPIs include total and recent pass-rate snapshots per line, enabling stability tracking over repeated runs.

### 5) Dashboard upgraded to consume line-level LTS evidence

`tools/release_gate_dashboard.ps1` was extended to:

- read `unity_miq_lts_gate_summary.json` when available
- show `Unity MIQ LTS Gate (Overall)`
- show per-line rows (e.g., `Unity MIQ LTS Line [2022-lts]`)
- prefer LTS gate result as the primary Unity compatibility decision signal

Fallback behavior remains for environments where only legacy artifacts exist.

### 6) Baseline/readiness summaries now expose stability artifacts

Both scripts now list LTS evidence artifacts in their generated summary outputs:

- `tools/run_quality_baseline.ps1`
- `tools/release_readiness_gate.ps1`

This ensures operators can verify not only pass/fail but also historical stability context from standard entry points.

## Verification Summary

- PowerShell parser validation was run for modified gate/dashboard scripts.
- Script-level policy and artifact wiring were diff-reviewed.
- Live multi-line Unity editor execution is environment-dependent and must be confirmed on configured runners.

## Stability Impact

Positive impact:

- stronger fail-fast behavior for environment misconfiguration
- clearer per-line failure diagnosis
- improved release gating confidence through explicit official-line policy
- trend-capable evidence artifacts for stability monitoring

Residual risk:

- runner env-var/setup drift still causes hard failures (intended)
- support confidence remains proportional to actual recurring gate execution cadence

## Recommended Next Steps

1. Run `unity_miq_lts_gate.ps1` on CI runners with all three editor env vars configured and archive first KPI baseline.
2. Establish a recurring schedule (daily or per-PR) to accumulate history and compute pass-rate trend.
3. Define operational SLOs (for example, recent pass rate threshold per line) and alert policy for regression spikes.
