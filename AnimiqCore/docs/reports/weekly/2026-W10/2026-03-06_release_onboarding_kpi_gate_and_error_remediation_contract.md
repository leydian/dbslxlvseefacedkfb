# Release Onboarding KPI Gate and Error Remediation Contract (2026-03-06)

## Summary

This update implements execution slices from the 10-persona action plan:

- onboarding KPI threshold policy is now consumable by release gate scripts
- release dashboard can fail-close on onboarding KPI conditions
- public error code docs now include an action contract for diagnostics -> remediation -> repro flow

Scope is limited to release tooling and public troubleshooting documentation.

## Implemented Changes

### 1) Release dashboard onboarding KPI policy support

Updated:

- `tools/release_gate_dashboard.ps1`

Added:

- policy inputs:
  - `RequireOnboardingKpiForWpfOnly`
  - `RequireOnboardingKpiForFull` (default `true`)
  - `OnboardingWithin3MinSuccessRateThresholdPct` (default `70.0`)
  - `OnboardingMinSessionCount` (default `5`)
- onboarding KPI state reader from `build/reports/onboarding_kpi_summary.json`
- gate summary fields for onboarding KPI status/pass/rate/session count
- release candidate evaluation now optionally includes onboarding KPI requirement for:
  - `ReleaseCandidateWpfOnly`
  - `ReleaseCandidateFull`

### 2) Release readiness gate orchestration support

Updated:

- `tools/release_readiness_gate.ps1`

Added:

- onboarding KPI options:
  - `RequireOnboardingKpiForWpfOnly`
  - `RequireOnboardingKpiForFull` (default `true`)
  - `OnboardingWithin3MinSuccessRateThresholdPct`
  - `OnboardingMinSessionCount`
  - `OnboardingTelemetryPath` (default `.\build\reports\telemetry_latest.json`)
  - `SkipOnboardingKpiSummary`
- optional `Onboarding KPI summary` step invocation:
  - runs `tools/onboarding_kpi_summary.ps1` when telemetry exists
  - skips safely with explicit log when telemetry is missing
- dashboard invocation now forwards onboarding KPI policy arguments
- summary/artifact section now lists onboarding KPI policy and artifact outputs

### 3) Public error remediation contract

Updated:

- `docs/public/error-codes.md`

Added:

- explicit troubleshooting flow:
  - error/warning 확인 -> 조치 -> diagnostics bundle -> repro 명령 재실행
- diagnostics bundle artifact references:
  - `repro_commands.txt`
  - `environment_snapshot.json`
  - `telemetry.json`
  - `onboarding_kpi_summary.txt`
- common host tracking/runtime error code guidance (parse/drop/input/webcam/mediapipe classes)

## Verification Summary

Executed:

```powershell
powershell -NoProfile -Command "[void][scriptblock]::Create((Get-Content -Raw 'tools/release_gate_dashboard.ps1'))"
powershell -NoProfile -Command "[void][scriptblock]::Create((Get-Content -Raw 'tools/release_readiness_gate.ps1'))"
```

Outcome:

- PowerShell parse validation: PASS (`2/2`)

## Known Risks or Limitations

- onboarding KPI gate becomes meaningful only after telemetry baseline files are present.
- default threshold (`70%`, min sessions `5`) is a bootstrap value and should be calibrated from observed trend data.

## Next Steps

1. Pin onboarding KPI threshold policy in release runbook after two-week baseline review.
2. Add CI job assertion that reports onboarding KPI gate state in release artifacts.
