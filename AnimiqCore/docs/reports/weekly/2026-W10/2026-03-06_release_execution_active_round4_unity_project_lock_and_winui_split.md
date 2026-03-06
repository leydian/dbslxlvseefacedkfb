# Release Execution Active Round 4: Unity Project Lock and WinUI Split Recheck (2026-03-06)

## Summary

Executed the immediate action plan with focus on:

1. WinUI failure-class split verification (`NoRestore` vs restore-enabled)
2. Unity/MIQ gate recovery attempt using discovered local Unity `2021.3.18f1` editor and matching project
3. WPF/tracking baseline revalidation and dashboard refresh

Result:

- `ReleaseCandidateWpfOnly: PASS`
- `ReleaseCandidateFull: FAIL`

## Implemented/Executed

### 1) WinUI split verification

Executed:

- `tools/winui_xaml_min_repro.ps1 -NoRestore` (dedicated output paths)
- `tools/winui_xaml_min_repro.ps1` (restore-enabled, dedicated output paths)
- `tools/winui_blocker_triage.ps1 -NoRestore`

Observed:

- both lanes converge to:
  - `FailureClass: TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
  - `WMC9999` at `Microsoft.UI.Xaml.Markup.Compiler.interop.targets:509:9`
- toolchain/xaml unsupported classification remains stable regardless of restore toggle.

### 2) Unity/MIQ gate recovery attempt

Resolved runtime prerequisites:

- Unity editor:
  - `C:\Program Files\Unity\Hub\Editor\2021.3.18f1\Editor\Unity.exe`
- Unity project candidate:
  - `D:\Unity\My project sdk 연구용`
  - `ProjectVersion: 2021.3.18f1`

Executed:

- `tools/unity_miq_validate.ps1` with explicit editor/project paths
- `tools/miq_compression_quality_gate.ps1` with explicit editor/project paths
- `tools/miq_parity_gate.ps1` with explicit editor/project paths

Observed blocker:

- Unity batch runs failed because the selected Unity project was already open/locked by another Unity instance context.
- log evidence includes:
  - `It looks like another Unity instance is running with this project open.`
  - `Multiple Unity instances cannot open the same project.`

Interpretation:

- the previous `PRECONDITION_FAILED (editor/project path missing)` blocker was reduced to a concrete execution blocker: `PROJECT_LOCKED_BY_ANOTHER_UNITY_INSTANCE`.

### 3) WPF/tracking baseline revalidation

Executed:

- `tools/host_e2e_gate.ps1 -SkipNativeBuild -NoRestore`
- `tools/tracking_parser_fuzz_gate.ps1` (re-run after one transient file-lock retry)
- `tools/onboarding_kpi_summary.ps1`
- `tools/onboarding_kpi_calibrate.ps1`
- `tools/release_gate_dashboard.ps1`

Observed:

- Host E2E: `PASS`
- Tracking fuzz: `PASS`
- Onboarding KPI still below policy sample floor:
  - `SessionCount: 1`
  - policy min sessions: `5`
- dashboard remains:
  - `ReleaseCandidateWpfOnly: PASS`
  - `ReleaseCandidateFull: FAIL`

## Verification Artifacts

- `build/reports/winui_xaml_min_repro_summary_norestore.txt`
- `build/reports/winui_xaml_min_repro_summary_restore.txt`
- `build/reports/winui_blocker_triage_summary.txt`
- `build/reports/unity_miq_2021-lts_editmode.log`
- `build/reports/unity_miq_2021-lts_compression_gate.log`
- `build/reports/unity_miq_2021-lts_parity_gate.log`
- `build/reports/host_e2e_gate_summary.txt`
- `build/reports/tracking_parser_fuzz_gate_summary.txt`
- `build/reports/onboarding_kpi_summary.txt`
- `build/reports/release_gate_dashboard.txt`

## Current Blockers

1. WinUI toolchain blocker (`WMC9999`) still unresolved.
2. Unity/MIQ execution lane currently blocked by Unity project lock (project open elsewhere).
3. Onboarding KPI gate still `INSUFFICIENT_SAMPLES` (`sessions=1`, requires `>=5`).
