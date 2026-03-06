# Release ROI Execution Hardening (2026-03-06)

## Summary

Implemented the agreed ROI-first execution slice to improve release decision quality, performance budget enforcement, soak regression detection, and diagnostics reproducibility without changing runtime feature scope.

Primary outcomes:

- release readiness summary now mirrors dashboard candidate results from one JSON source
- render performance gate now supports desktop-targeted budgets and live-tick provenance quality checks
- avatar soak gate now fails on per-sample regressions (not only global pass ratio)
- diagnostics bundle now includes a machine-readable manifest with reproducibility hashes and gate contract metadata

## Changes Applied

### 1) Release gate SSOT + failure-safe summary

Updated:

- `tools/release_readiness_gate.ps1`

Changes:

- added policy passthrough options for baseline gates:
  - `RenderPerfProfile`, `RenderPerfTargetFps`, `RenderPerfMinLiveTickSampleRatio`
  - `SoakIterationsPerSample`, `SoakMinSuccessRatio`, `SoakMinPerSampleSuccessRatio`
- added fail-safe summary writing on exceptions (summary is emitted, then script exits non-zero)
- release summary now records dashboard-derived values from `build/reports/release_gate_dashboard.json`:
  - `DashboardTrackingContract`
  - `DashboardReleaseCandidateWpfOnly`
  - `DashboardReleaseCandidateFull`

### 2) Release dashboard policy traceability

Updated:

- `tools/release_gate_dashboard.ps1`

Changes:

- added `PolicyVersion` parameter (`2026-03-06.1` default)
- added dashboard rows for release-readiness policy snapshot:
  - render perf profile
  - soak minimum success ratio
- `gate_summary` JSON now includes `policy_version`

### 3) Render performance budget refinement

Updated:

- `tools/render_perf_gate.ps1`

Changes:

- added profiles: `desktop-60`, `desktop-30`
- added `TargetFps`-driven budget derivation for p95/p99/drop threshold when explicit values are not provided
- added `MinLiveTickSampleRatio` gate to enforce metrics provenance quality
- summary now includes:
  - `TargetFps`
  - `LiveTickSampleRatio`
  - `GateLiveTick`

### 4) Soak gate regression sensitivity uplift

Updated:

- `tools/avatar_load_soak_gate.ps1`

Changes:

- added `MinPerSampleSuccessRatio`
- gate now fails when any sample falls below per-sample ratio threshold
- summary now includes first failure snippet per sample (`first_failure`)

### 5) Baseline orchestrator wiring

Updated:

- `tools/run_quality_baseline.ps1`

Changes:

- added passthrough arguments for new render/soak policy controls
- baseline summary now prints the active policy values for reproducibility

### 6) Diagnostics bundle reproducibility manifest

Updated:

- `host/HostCore/HostController.MvpFeatures.cs`

Changes:

- diagnostics bundle now emits `diagnostics_manifest.json`
- manifest includes:
  - `manifest_version`
  - `gate_contract_version`
  - session fingerprint fields (`metrics_session_id`, parser/profile info, hashed avatar path)
  - SHA256 hashes for key bundle artifacts (`telemetry/snapshot/preflight/environment/repro/kpi`)
- environment snapshot now includes `ANIMIQ_MEDIAPIPE_PYTHON`
- repro commands updated to include explicit release-readiness policy arguments

## Verification Summary

Executed in this workspace:

- script syntax validation:
  - `tools/render_perf_gate.ps1`: PASS
  - `tools/run_quality_baseline.ps1`: PASS
  - `tools/avatar_load_soak_gate.ps1`: PASS
  - `tools/release_readiness_gate.ps1`: PASS
  - `tools/release_gate_dashboard.ps1`: PASS
- build validation:
  - `dotnet build host/HostCore/HostCore.csproj -c Release -nologo`: PASS (`0 warnings`, `0 errors`)

## Known Risks / Limitations

- WinUI toolchain blocker resolution is still out of scope for this slice.
- Full release-candidate status remains dependent on runtime gate artifacts being freshly produced in CI/local runs.
- Diagnostics manifest fingerprints increase reproducibility, but they do not replace full artifact capture for non-deterministic environment issues.

## Next Steps

1. Run `release_readiness_gate.ps1` in CI with explicit policy values to populate dashboard evidence end-to-end.
2. Promote policy defaults (`desktop-60`, soak per-sample threshold) into release runbook documentation.
3. Add dashboard validation check to CI to fail when summary/dashboard candidate states diverge.
