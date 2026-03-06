# XAV2 LTS KPI Risk Signal + Dashboard Fail-Closed Update (2026-03-06)

## Summary

This update hardens the Unity LTS support operation model for XAV2 after
officially promoting `2021-lts`, `2022-lts`, and `2023-lts` as official lines.

Primary outcomes:

- LTS gate defaults now treat all three lines as official.
- Release dashboard now behaves fail-closed when LTS gate reports `FAIL`.
- Dashboard now surfaces a trend risk signal using recent LTS pass-rate KPI.

## Detailed Changes

### 1) LTS policy defaults (official 3 lines)

Updated:

- `tools/unity_xav2_lts_gate.ps1`

Changes:

- default `OfficialLinesCsv` set to:
  - `2021-lts,2022-lts,2023-lts`
- default `CandidateLinesCsv` set to empty

Operational effect:

- all three LTS lines are part of the default official blocking policy:
  `official-lines-all-pass-required`

### 2) Dashboard fail-closed behavior for Unity XAV2

Updated:

- `tools/release_gate_dashboard.ps1`

Changes:

- Unity XAV2 final status evaluation now uses explicit state branching:
  - `PASS` when LTS gate is `PASS`
  - `FAIL` when LTS gate is `FAIL`
  - fallback to legacy single-line signals only when LTS gate is missing/unknown

Operational effect:

- if any official LTS line fails and LTS gate overall is `FAIL`,
  release candidate status cannot remain green via legacy fallback.

### 3) Recent-pass-rate risk signal (trend visibility)

Updated:

- `tools/release_gate_dashboard.ps1`

Changes:

- reads `build/reports/unity_xav2_lts_kpi_summary.json` when available
- emits risk fields in dashboard JSON:
  - `unity_xav2_lts_recent_risk`
  - `unity_xav2_lts_recent_risk_lines`
- emits risk summary lines in dashboard text output:
  - `UnityXav2LtsRecentRisk`
  - `UnityXav2LtsRecentRiskLines`

Risk rule:

- if any official line has `recent_pass_rate_pct < 100`, mark risk = `true`

### 4) Documentation synchronization

Updated:

- `CHANGELOG.md`
- `docs/reports/weekly/2026-W10/2026-03-06_xav2_unity_lts_matrix_gate_policy.md`

Changes:

- aligned wording to unified 3-line official support model
- documented dashboard fail-closed behavior and KPI risk signal

## Verification

Executed:

```powershell
[scriptblock]::Create((Get-Content -Raw .\tools\release_gate_dashboard.ps1))
[scriptblock]::Create((Get-Content -Raw .\tools\unity_xav2_lts_gate.ps1))
[scriptblock]::Create((Get-Content -Raw .\tools\run_quality_baseline.ps1))
[scriptblock]::Create((Get-Content -Raw .\tools\release_readiness_gate.ps1))
```

Result:

- PowerShell parse checks passed (`PARSE_OK`) for all updated scripts.

## Notes

- Full Unity execution gates are environment-dependent and were not run in this
  update step.
