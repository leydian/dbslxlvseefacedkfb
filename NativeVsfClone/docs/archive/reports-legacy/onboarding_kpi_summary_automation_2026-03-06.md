# Onboarding KPI Summary Automation (2026-03-06)

## Scope

This report covers HostCore and tooling changes that automate onboarding KPI aggregation from telemetry.

In scope:
- diagnostics bundle KPI summary artifact generation
- in-process telemetry snapshot and rollup helpers
- offline PowerShell KPI summarization script

Out of scope:
- UI layout/interaction changes
- telemetry schema expansion beyond onboarding KPI fields already introduced

## Implemented Changes

- Diagnostics bundle export now includes onboarding KPI summary:
  - `host/HostCore/HostController.MvpFeatures.cs`
  - new artifact: `onboarding_kpi_summary.txt`
- Added HostCore KPI rollup builder:
  - computes per-session rollup keyed by `session_started_at`
  - session counts:
    - total sessions
    - sessions with output started
    - sessions with `within_3min_success = true`
  - computes success rate and output milestone counters
- Added telemetry snapshot API:
  - `host/HostCore/PlatformFeatures.cs`
  - `TelemetryService.Snapshot()` returns copied event dictionaries for read-only aggregation
- Added offline script:
  - `tools/onboarding_kpi_summary.ps1`
  - input: telemetry JSON array
  - outputs:
    - `build/reports/onboarding_kpi_summary.json`
    - `build/reports/onboarding_kpi_summary.txt`
- Added KPI script invocation hint to diagnostics repro commands:
  - `host/HostCore/HostController.MvpFeatures.cs`

## Verification Summary

Executed:

```powershell
dotnet build host\WpfHost\WpfHost.csproj -c Release
powershell -ExecutionPolicy Bypass -File .\tools\onboarding_kpi_summary.ps1 -TelemetryPath .\build\reports\telemetry_latest.json -OutputJson .\build\reports\onboarding_kpi_summary.json -OutputTxt .\build\reports\onboarding_kpi_summary.txt
```

Outcome:
- build: PASS
- KPI summary script smoke: PASS

Artifacts:
- `build/reports/onboarding_kpi_summary.json`
- `build/reports/onboarding_kpi_summary.txt`

## Known Risks or Limitations

- Session identity is currently inferred from `session_started_at`; if that field is missing in telemetry input, those rows are ignored.
- KPI summary is single-host/local-file based and not yet aggregated across multiple machines/runs.
- Existing telemetry redaction policy remains unchanged and may mask path-like fields.

## Next Steps

1. Add weekly aggregation automation that merges multiple telemetry files into a single onboarding KPI trend.
2. Add a gate check threshold for `within_3min_success_rate_pct` once baseline data is stable.
