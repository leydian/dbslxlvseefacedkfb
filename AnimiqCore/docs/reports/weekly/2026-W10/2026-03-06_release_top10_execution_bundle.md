# Release Top10 Execution Bundle (2026-03-06)

## Summary

This pass implemented the previously agreed 10 high-impact actions as concrete automation/workflow/UI changes.

Primary goals:

1. keep `WPF-only` lane releasable with deterministic checks
2. reduce `Full` lane uncertainty by hardening WinUI/Unity/KPI blockers as explicit gates
3. improve operator recovery speed from tracking runtime failures

## Implemented (10 items)

### 1) WinUI known-good lane fingerprinting

- Added environment fingerprint script:
  - `tools/winui_env_fingerprint.ps1`
- Captures lane id + SDK/dotnet/NuGet readiness for WinUI repro normalization.

### 2) WinUI minimal repro always-on CI lane

- Added dedicated workflow:
  - `.github/workflows/winui-min-repro.yml`
- Also integrated fingerprint collection into existing host publish WinUI diagnostics lane:
  - `.github/workflows/host-publish.yml`

### 3) Unity project lock preflight

- Added lock check script:
  - `tools/unity_project_lock_check.ps1`
- Wired preflight into:
  - `tools/unity_Miq_validate.ps1`
  - `tools/unity_Miq_lts_gate.ps1`

### 4) Unity MIQ 3-track one-shot bundle

- Added bundle runner:
  - `tools/unity_miq_gate_bundle.ps1`
- Updated CI to run bundle instead of separated individual calls:
  - `.github/workflows/unity-miq-compat.yml`

### 5) Onboarding KPI sample seeding utility

- Added telemetry seeding helper:
  - `tools/onboarding_kpi_seed_telemetry.ps1`
- Purpose: unblock KPI sufficiency checks in controlled/test lanes.

### 6) Release dashboard input missing guard

- Added strict input presence gate:
  - `tools/release_dashboard_input_guard.ps1`
- Wired into readiness orchestrator:
  - `tools/release_readiness_gate.ps1`
- Added policy-level missing-input fail reason support:
  - `tools/release_gate_dashboard.ps1`

### 7) WPF user-flow regression pack

- Added WPF-centric operational regression bundle:
  - `tools/wpf_user_regression_pack.ps1`
- Includes WPF publish/smoke + host E2E + tracking fuzz + dashboard refresh.

### 8) Operator quick recovery action path

- Added error-code-to-action recovery script:
  - `tools/tracking_error_recovery.ps1`
- Wired as automation contract first (script-level quick action) for immediate operational use.

### 9) Split release lane execution gate

- Added dual-lane gate orchestrator:
  - `tools/release_dual_lane_gate.ps1`
- Explicitly executes and reports both `WPF_ONLY` and `FULL`.

### 10) Weekly blocker burndown document automation

- Added blocker burndown generator:
  - `tools/release_blocker_burndown.ps1`
- Wired into nightly stability workflow:
  - `.github/workflows/host-stability-nightly.yml`

## Additional Updates

- README gate command section expanded with new operational commands:
  - `README.md`

## Verification

Executed in current workspace:

- `tools/winui_env_fingerprint.ps1`: PASS
- `tools/unity_project_lock_check.ps1`: PASS
- `tools/tracking_error_recovery.ps1 -ErrorCode TRACKING_MEDIAPIPE_NO_FRAME`: PASS
- `tools/release_blocker_burndown.ps1`: PASS
- `tools/release_gate_dashboard.ps1 -FailOnMissingInputsForFull`: PASS
- parse checks for all newly added/modified PS scripts: PASS

Known environment limitation during validation:

- `dotnet build host/WpfHost/WpfHost.csproj` failed with `NU1301` due missing local NuGet mirror path in this environment, so compile verification is blocked by package source configuration, not by script syntax.
