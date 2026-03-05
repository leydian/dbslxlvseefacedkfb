# Host Blocker Status Board (2026-03-05)

## Summary

Single status board for host-track closure work across WPF-first release gating and WinUI diagnostics recovery.

## Open Blockers

1. WinUI compile blocker remains open.
   - current local signature: preflight/toolchain fail before publish (`MISSING_MSBUILD_DISCOVERY`) plus restore/network failure (`NU1301`)
   - current class: `TOOLCHAIN_VISUAL_STUDIO_INCOMPLETE`
   - latest evidence: `build/reports/winui/winui_diagnostic_manifest.json`

2. WPF non-interactive launch smoke is unstable in CLI/headless runs.
   - signature: process exit `-532462766`
   - evidence path: `build/reports/wpf_launch_smoke_latest.txt`
   - historical event-log anchor: `.NET Runtime` event `1026`, `System.DllNotFoundException`

## Closed Items

1. WPF-first publish/gate baseline is stable.
   - `publish_hosts.ps1` WPF publish path: PASS in latest rounds
   - `vsfavatar_quality_gate.ps1 -UseFixedSet`: PASS (`HostTrackStatus=PASS_WPF_BASELINE`)
   - `run_quality_baseline.ps1`: PASS

2. WinUI diagnostics collection contract is deterministic.
   - preflight probe + failure class + binlog/log artifacts produced on failure path
   - profile-based diagnostics (`diag-default`, `managed-xaml`) persisted in manifest
   - local rerun comparison (`run1` vs `run2`) shows no class/profile drift:
     - `failure_class=TOOLCHAIN_VISUAL_STUDIO_INCOMPLETE` (same)
     - profiles exit/hints (same)
     - evidence: `build/reports/winui_manifest_diff_run1_vs_run2.txt`

## Next Actions

1. Compare WinUI diagnostics across CI matrix (`windows-latest`, `windows-2022`) and local run using `tools/compare_winui_diag_manifest.ps1`.
2. Resolve preflight blockers in this local environment (`MISSING_MSBUILD_DISCOVERY`) and rerun to reach publish-stage WinUI diagnostics.
3. Resolve WPF launch failure dependency chain (`exit=-532462766`) and stabilize smoke probe.
4. Capture quantitative before/after metrics for refresh-throttle (`LastFrameMs`, UI update cadence, logs-tab active/inactive impact).

## Latest Evidence Snapshot (2026-03-05, follow-up execution)

- `publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi` rerun x2:
  - `run1`: `2026-03-05T17:22:15.0329753+09:00`
  - `run2`: `2026-03-05T17:23:04.7308102+09:00`
  - both runs:
    - WPF publish: FAIL (`NU1301` restore/network path)
    - WinUI preflight: FAIL (`MISSING_MSBUILD_DISCOVERY`)
    - diagnostics collected with profile outputs
- manifest rerun diff:
  - `build/reports/winui_manifest_diff_run1_vs_run2.txt`
  - result: all tracked fields `SAME`
- WPF launch smoke direct rerun:
  - timestamp: `2026-03-05T17:25:43.9247110+09:00`
  - result: `FAIL`, `ExitCode=-532462766`

## Artifact Contract

- `build/reports/host_publish_latest.txt`
- `build/reports/wpf_launch_smoke_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build.binlog`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_managed_diag.log`
- `build/reports/winui/obj-dump/**`
