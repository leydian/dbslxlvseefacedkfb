# Release Execution Active Round 2 (2026-03-06)

## Summary

This pass executed immediate release-priority items and hardened release-state interpretation for the current workspace.

Completed outcomes:

- `WPF-only` and `Full` release-candidate criteria are now explicitly separable in dashboard policy
- WinUI failure diagnostics were de-noised to reduce false NuGet-auth hints
- WinUI blocker was validated as environment/toolchain-level (`WMC9999`) with minimal repro
- Unity XAV2 env bootstrap script was added to standardize local/CI prerequisites discovery

## Changes Applied

### 1) Release candidate policy split (`WPF-only` vs `Full`)

Updated:

- `tools/release_gate_dashboard.ps1`
- `tools/release_readiness_gate.ps1`

New policy switches:

- `RequireUnityXav2ForWpfOnly` (default: `False`)
- `RequireUnityXav2ForFull` (default: `True`)

Behavior:

- `WPF-only` now gates on avatar + WPF host pass by default
- `Full` continues to require WinUI pass and Unity XAV2 gate pass
- dashboard text/json now prints active policy values

Verification snapshot:

- with default policy:
  - `ReleaseCandidateWpfOnly: PASS`
  - `ReleaseCandidateFull: FAIL`
- with strict policy (`RequireUnityXav2ForWpfOnly=True`):
  - `ReleaseCandidateWpfOnly: FAIL`
  - `ReleaseCandidateFull: FAIL`

### 2) WinUI diagnostics signal cleanup

Updated:

- `tools/publish_hosts.ps1`

Change:

- narrowed NuGet auth detection from broad raw `401/403` substring matching to explicit auth-failure signatures only

Result:

- `winui_diagnostic_manifest.json` root cause hints now focus on actual XAML toolchain failure:
  - `XamlCompiler.exe exit path observed (MSB3073)`
  - `Managed XAML compiler reported WMC9999`

### 3) Unity XAV2 environment bootstrap

Added:

- `tools/unity_xav2_env_bootstrap.ps1`

Artifacts:

- `build/reports/unity_xav2_env_bootstrap_summary.txt`
- `build/reports/unity_xav2_env_bootstrap_summary.json`

Current machine result:

- Unity editor auto-detected (`2021.3.18f1`)
- Unity project path not found in repo context (`Status: PARTIAL`)

### 4) WinUI blocker confirmation (minimal repro)

Executed:

- temporary minimal WinUI project build under `build/tmp_winui_min`

Observed:

- same `WMC9999` reproduced on minimal project
- confirms blocker is not caused by repository XAML complexity

## Current State

- `VSFAvatar`: PASS
- `VRM`: PASS
- `VXAvatar`: PASS
- `Host Publish (WPF)`: PASS
- `Host Publish (WinUI)`: FAIL (`TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`)
- `Unity XAV2 Validate`: NOT_RUN
- `XAV2 Compression Quality`: MISSING
- `XAV2 Unity/Native Parity`: MISSING
- `ReleaseCandidateWpfOnly`: PASS (default split policy)
- `ReleaseCandidateFull`: FAIL

## Remaining blockers for full closure

1. provide/resolve `UNITY_XAV2_PROJECT_PATH` for gate scripts
2. execute Unity XAV2 validate/compression/parity gates
3. close WinUI `WMC9999` toolchain issue (environment lane)

