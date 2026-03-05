# Host Blocker Status Board (2026-03-05)

## Summary

Single status board for host-track closure work across WPF-first release gating and WinUI diagnostics recovery.

## Open Blockers

1. WinUI compile blocker remains open.
   - signature: `WMC9999` + `MSB3073` (`XamlCompiler.exe`)
   - current class: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
   - latest evidence: `build/reports/winui/winui_diagnostic_manifest.json`

2. WPF non-interactive launch smoke is unstable in CLI/headless runs.
   - signature: process exit `-532462766`
   - evidence path: `build/reports/wpf_launch_smoke_latest.txt`
   - event-log anchor: `.NET Runtime` event `1026`, `System.DllNotFoundException`

## Closed Items

1. WPF-first publish/gate baseline is stable.
   - `publish_hosts.ps1` WPF publish path: PASS in latest rounds
   - `vsfavatar_quality_gate.ps1 -UseFixedSet`: PASS (`HostTrackStatus=PASS_WPF_BASELINE`)
   - `run_quality_baseline.ps1`: PASS

2. WinUI diagnostics collection contract is deterministic.
   - preflight probe + failure class + binlog/log artifacts produced on failure path
   - profile-based diagnostics (`diag-default`, `managed-xaml`) persisted in manifest

## Next Actions

1. Compare WinUI diagnostics across CI matrix (`windows-latest`, `windows-2022`) and local run.
2. Resolve `DllNotFoundException` dependency chain for WPF launch smoke in headless flow.
3. Capture quantitative before/after metrics for refresh-throttle (`LastFrameMs`, UI update cadence, logs-tab active/inactive impact).

## Artifact Contract

- `build/reports/host_publish_latest.txt`
- `build/reports/wpf_launch_smoke_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build.binlog`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_managed_diag.log`
- `build/reports/winui/obj-dump/**`
