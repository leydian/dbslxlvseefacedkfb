# Release Execution Recheck: WinUI/Unity/KPI Status (2026-03-06)

## Summary

Re-ran the release-priority execution profile to confirm the true blocking state after tooling rollout.

Current candidate state:

- `ReleaseCandidateWpfOnly: PASS`
- `ReleaseCandidateFull: FAIL`

Primary unresolved blockers remain:

1. WinUI toolchain publish path (`WMC9999`)
2. Unity XAV2 gate inputs and execution path not green
3. Onboarding KPI gate status remains `INSUFFICIENT_SAMPLES` for full-governance mode

## Executed

### 1) WinUI blocker recheck and diagnostics refresh

Executed:

- `tools/winui_xaml_min_repro.ps1 -NoRestore`
- `tools/winui_blocker_triage.ps1 -NoRestore`
- `tools/winui_diag_matrix_summary.ps1`

Observed:

- failure class remained `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
- `WMC9999Count: 2`
- first diagnostic captured:
  - `Microsoft.UI.Xaml.Markup.Compiler.interop.targets:509:9`
- triage summary and matrix summary artifacts were regenerated

Artifacts:

- `build/reports/winui_xaml_min_repro_summary.txt`
- `build/reports/winui_xaml_min_repro_summary.json`
- `build/reports/winui_blocker_triage_summary.txt`
- `build/reports/winui_blocker_triage_summary.json`
- `build/reports/winui_manifest_matrix_summary_latest.txt`
- `build/reports/winui_manifest_matrix_summary_latest.json`

### 2) XAV2 gate corpus and gate run recheck

Executed:

- `tools/xav2_prepare_gate_corpus.ps1 -SourceDir . -OutputDir .\build\gate_corpus\xav2 -MinSampleCount 10 -IncludeBuildArtifacts`
- `tools/xav2_render_regression_gate.ps1` with corpus + manifest inputs

Observed:

- corpus preparation regenerated 10 samples and manifest
- gate summary/json artifacts refreshed for current workspace baseline

Artifacts:

- `build/gate_corpus/xav2/prepare_summary.txt`
- `build/gate_corpus/xav2/sample_manifest.json`
- `build/reports/xav2_render_regression_gate_summary.txt`
- `build/reports/xav2_render_regression_gate_summary.json`

### 3) Onboarding KPI recheck

Executed:

- `tools/onboarding_kpi_summary.ps1 -TelemetryPath .\build\reports\telemetry_latest.json`
- `tools/onboarding_kpi_calibrate.ps1 -TelemetryPath .\build\reports\telemetry_latest.json`

Observed:

- KPI summary still reports only one session in this telemetry snapshot
- dashboard gate view remains `INSUFFICIENT_SAMPLES` with policy min sessions `5`
- calibration recommendation remains baseline-default (`threshold=70`, `min_sessions=5`)

Artifacts:

- `build/reports/onboarding_kpi_summary.txt`
- `build/reports/onboarding_kpi_summary.json`
- `build/reports/onboarding_kpi_calibration.txt`
- `build/reports/onboarding_kpi_calibration.json`

### 4) Release dashboard refresh

Executed:

- `tools/release_gate_dashboard.ps1` (explicit report paths)

Observed:

- `ReleaseCandidateWpfOnly: PASS`
- `ReleaseCandidateFull: FAIL`
- host mode reflected WinUI-included publish context with WinUI publish failure

Artifact:

- `build/reports/release_gate_dashboard.txt`
- `build/reports/release_gate_dashboard.json`

## Current Blocking Facts

1. WinUI publish remains hard-fail in current environment:
   - `WMC9999` (platform/toolchain unsupported path)
2. NuGet probe indicates no reachable enabled remote source in this environment.
3. Unity XAV2 full-chain gates are still not green in dashboard status.
4. Onboarding KPI policy remains below session-count sufficiency for full gating.

## Next Steps

1. Resolve/route WinUI environment lane to a known-good toolchain host and compare manifests across local + CI.
2. Provide a valid `UNITY_XAV2_PROJECT_PATH` and execute Unity XAV2 validate/compression/parity tracks in one lane.
3. Increase onboarding telemetry session count to meet policy minimum and re-evaluate KPI gate status.
