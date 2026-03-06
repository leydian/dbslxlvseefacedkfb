# Release Live Update: VX Policy Alignment and WinUI Repro (2026-03-06)

## Summary

This pass captured the live execution outcome after starting the prioritized release actions.

Key outcomes:

- release state was re-synchronized with fresh gate runs
- `VXAvatar` track recovered from false-fail to pass by aligning gate behavior with the current runtime support policy
- WinUI failure was isolated from NuGet-auth noise and converged to a stable toolchain/XAML failure class

## Why this change was needed

Current runtime policy (documented on 2026-03-05) supports only:

- `.vrm`
- `.miq`
- `.vsfavatar`

Legacy `.vxavatar/.vxa2` runtime loading was intentionally removed.  
However, `tools/vxavatar_quality_gate.ps1` still hard-required legacy rows and failed `GateA/B/C/D` when those formats produced no parser fields (because runtime no longer loads them).

This created a policy-vs-gate mismatch and produced an avoidable dashboard failure.

## Implemented Changes

### 1) Gate policy alignment in `tools/vxavatar_quality_gate.ps1`

Added:

- `-EnableLegacyVxVxa2Checks` switch (default: disabled)

Behavior update:

- default gate scope now validates active support path (`MIQ`) and skips legacy VX/VXA2 checks
- legacy checks (`GateA/B/C`) remain available when explicitly enabled
- `GateD` required-field checks now apply only to kinds included in active gate scope
- summary output now records `EnableLegacyVxVxa2Checks` to make run profile explicit

Result:

- false failures from unsupported legacy formats are removed from default release gate runs

### 2) Release status re-sync execution

Executed:

- `tools/release_gate_dashboard.ps1`
- `tools/release_readiness_gate.ps1 -SkipVersionContractCheck -SkipQualityBaseline -SkipNativeBuild -NoRestore`
- `tools/release_readiness_gate.ps1 -IncludeWinUi -SkipVersionContractCheck -SkipQualityBaseline -SkipNativeBuild -NoRestore`
- `tools/vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`
- `tools/release_gate_dashboard.ps1` (refresh after gate fix)

Observed:

- `VXAvatar` transitioned to `Overall: PASS`
- `ReleaseCandidateWpfOnly` remained `FAIL` due to MIQ Unity gate rows still `NOT_RUN/MISSING`
- `ReleaseCandidateFull` remained `FAIL` due to WinUI publish failure

### 3) WinUI failure-class convergence

Executed:

- `tools/nuget_mirror_bootstrap.ps1`
- `tools/publish_hosts.ps1 -IncludeWinUi -SkipNativeBuild -NuGetSourceMode offline-only`
- `tools/winui_xaml_min_repro.ps1 -NoRestore`

Observed:

- NuGet-auth failure class was no longer dominant under `offline-only`
- WinUI still failed consistently with XAML compiler path
- minimal repro summary classified failure as:
  - `FailureClass: TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
  - hint includes `WMC9999`

Artifacts:

- `build/reports/release_gate_dashboard.txt`
- `build/reports/release_gate_dashboard.json`
- `build/reports/host_publish_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui_xaml_min_repro_summary.txt`

## Current State Snapshot (as of 2026-03-06)

- `VSFAvatar`: PASS
- `VRM`: PASS
- `VXAvatar`: PASS (after policy-aligned gate behavior)
- `Host Publish (WPF)`: PASS
- `Host Publish (WinUI)`: FAIL
- `Unity MIQ Validate`: NOT_RUN
- `MIQ Compression Quality`: MISSING
- `MIQ Unity/Native Parity`: MISSING
- `ReleaseCandidateWpfOnly`: FAIL
- `ReleaseCandidateFull`: FAIL

## Next Focus

1. close WinUI `WMC9999` toolchain issue (project/sdk/xaml-compiler compatibility track)
2. execute MIQ Unity validation/compression/parity gates in an environment with a resolvable Unity project path
3. re-run release dashboard/readiness to confirm candidate-state recovery

