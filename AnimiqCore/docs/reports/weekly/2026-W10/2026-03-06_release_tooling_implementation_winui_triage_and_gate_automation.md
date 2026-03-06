# Release Tooling Implementation: WinUI Triage + Gate Automation (2026-03-06)

## Summary

Implemented the execution pass for immediate release operations with focus on:

- line-level WinUI XAML blocker diagnostics (`WMC9999`) with machine-readable artifacts
- actionable automation around WinUI blocker triage and matrix comparison
- MIQ gate corpus preparation (10-sample baseline manifest generation)
- onboarding KPI threshold calibration tooling
- release gate orchestration and CI wiring updates

This slice does not resolve the WinUI toolchain blocker itself; it hardens diagnosis and operational repeatability.

## Changes Applied

### 1) WinUI minimal repro diagnostics hardening

Updated:

- `tools/winui_xaml_min_repro.ps1`

Added:

- `-SummaryJsonPath` output (`build/reports/winui_xaml_min_repro_summary.json`)
- line-level diagnostic parsing from `dotnet -v:diag` log
- extracted fields:
  - `DiagnosticEntryCount`
  - `WMC9999Count`
  - `FirstDiagnostic` (`file:line:column + code + message`)
- fallback `WMC9999` detection when standard diagnostic regex shape is not emitted

Result:

- WinUI failure now emits reproducible, structured root-cause evidence suitable for CI artifact review.

### 2) WinUI matrix summarization extension

Updated:

- `tools/winui_diag_matrix_summary.ps1`

Added:

- optional manifest input mode (auto-discovers default manifest candidates when none are provided)
- lane derivation (`local`, `windows-latest`, `windows-2022`, fallback `unknown`)
- JSON summary output (`build/reports/winui_manifest_matrix_summary_latest.json`)
- convergence summary fields across manifests

Result:

- local/CI WinUI failure-class convergence can be tracked from one canonical summary pair (`txt/json`).

### 3) New WinUI blocker triage orchestrator

Added:

- `tools/winui_blocker_triage.ps1`

Behavior:

- runs `winui_xaml_min_repro.ps1` and captures latest classifier + first diagnostic
- aggregates available WinUI manifests through `winui_diag_matrix_summary.ps1`
- writes consolidated outputs:
  - `build/reports/winui_blocker_triage_summary.txt`
  - `build/reports/winui_blocker_triage_summary.json`

Result:

- one command now produces the operator-ready WinUI blocker brief.

### 4) MIQ gate corpus preparation automation

Added:

- `tools/miq_prepare_gate_corpus.ps1`

Behavior:

- discovers `.miq` files from a source root
- materializes a curated 10-sample corpus into `build/gate_corpus/miq`
- emits gate manifest:
  - `build/gate_corpus/miq/sample_manifest.json`
- emits preparation summary:
  - `build/gate_corpus/miq/prepare_summary.txt`
- guards against recursive self-ingestion from output directory

Result:

- `GateX0` minimum-sample prerequisite is operationally reproducible.

### 5) Onboarding KPI calibration utility

Added:

- `tools/onboarding_kpi_calibrate.ps1`

Behavior:

- reads telemetry baseline
- computes observed within-3-min success rate and latency percentiles (`p25/p50/p75`)
- recommends:
  - threshold percent (`recommended_threshold_pct`)
  - minimum session count (`recommended_min_session_count`)
- writes:
  - `build/reports/onboarding_kpi_calibration.txt`
  - `build/reports/onboarding_kpi_calibration.json`

Result:

- onboarding KPI gate policy can be tuned from data rather than static assumptions.

### 6) Cross-host onboarding state smoke check

Added:

- `tools/host_onboarding_state_smoke.ps1`

Behavior:

- verifies WPF/WinUI onboarding actionability surfaces and token wiring presence
- outputs:
  - `build/reports/host_onboarding_state_smoke_summary.txt`

Result:

- fast static parity guard for `READY/BLOCKED` onboarding state affordances.

### 7) Release readiness gate orchestration expansion

Updated:

- `tools/release_readiness_gate.ps1`

Added switches:

- `-EnableHostOnboardingStateSmoke`
- `-EnableWinUiBlockerTriage`
- `-EnableMiqCorpusPrep`
- `-MiqCorpusSourceDir`
- `-MiqCorpusOutputDir`
- `-MiqCorpusMinSampleCount`
- `-MiqCorpusIncludeBuildArtifacts`
- `-EnableOnboardingKpiCalibration`

Added artifact declarations:

- WinUI triage outputs (`txt/json`)
- WinUI min repro JSON
- onboarding KPI calibration outputs
- MIQ corpus prep outputs
- host onboarding smoke summary

Result:

- previously manual release-prep tasks are now script-selectable steps inside the gate wrapper.

### 8) CI workflow integration updates

Updated:

- `.github/workflows/host-publish.yml`

Added path triggers for new tools and diagnostics.

Added WPF lane steps:

- onboarding state smoke
- release dashboard refresh (onboarding KPI policy included)

Added WinUI diagnostics lane steps:

- run `winui_xaml_min_repro.ps1` (line-level diagnostics)
- run `winui_blocker_triage.ps1`
- upload extended artifacts including WinUI summary JSON outputs

Result:

- CI now publishes richer diagnostics for blocked WinUI runs without changing fail/pass semantics for existing blocker lanes.

## Verification Snapshot

Executed in this workspace:

- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\winui_xaml_min_repro.ps1 -NoRestore`
  - `FailureClass: TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
  - `WMC9999Count: 2`
  - first diagnostic path and line captured
- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\winui_blocker_triage.ps1 -NoRestore`
  - summary + json emitted
- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\winui_diag_matrix_summary.ps1`
  - matrix summary + json emitted
- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\miq_prepare_gate_corpus.ps1 -SourceDir . -OutputDir .\build\gate_corpus\miq -MinSampleCount 10 -IncludeBuildArtifacts`
  - `SampleCount: 10`
  - manifest emitted
- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\onboarding_kpi_calibrate.ps1 -TelemetryPath .\build\reports\telemetry_latest.json`
  - calibration summary + json emitted
- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\host_onboarding_state_smoke.ps1`
  - `Overall: PASS`
- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\release_gate_dashboard.ps1`
  - refreshed candidate status:
    - `ReleaseCandidateWpfOnly: PASS`
    - `ReleaseCandidateFull: FAIL`
    - `OnboardingKpiStatus: INSUFFICIENT_SAMPLES`

## Known Risks / Limitations

- WinUI `WMC9999` remains unresolved (toolchain/environment lane), but now emits actionable diagnostics consistently.
- release readiness runs with `-NoRestore` still fail early in host publish in this environment when required assets are not already restore-complete.
- onboarding KPI calibration quality depends on telemetry session coverage (`INSUFFICIENT_SAMPLES` currently observed).

## Next Steps

1. Run WinUI matrix on both CI lanes and feed manifests into matrix summary for convergence tracking.
2. Execute Unity MIQ validate/compression/parity gates once `UNITY_MIQ_PROJECT_PATH` is available.
3. Promote calibrated onboarding KPI thresholds into release runbook and CI policy defaults after baseline data window closes.
