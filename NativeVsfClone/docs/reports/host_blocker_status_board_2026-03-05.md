# Host Blocker Status Board (2026-03-05)

## Summary

Single status board for host-track closure work across WPF-first release gating and WinUI diagnostics recovery.

## Open Blockers

1. WinUI compile blocker remains open.
   - current local signature: WinUI publish-stage restore/network failure (`NU1301`)
   - current class: `NUGET_SOURCE_UNREACHABLE`
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
   - local rerun comparison (`run3` vs `run4`) shows no class/profile drift:
     - `failure_class=NUGET_SOURCE_UNREACHABLE` (same)
     - profiles exit/hints (same)
     - evidence: `build/reports/winui_manifest_diff_run3_vs_run4.txt`

## Next Actions

1. Compare WinUI diagnostics across CI matrix (`windows-latest`, `windows-2022`) and local run using `tools/compare_winui_diag_manifest.ps1`.
2. Resolve NuGet/network path (`NU1301`) so WPF/WinUI publish can reach compile-stage diagnostics reliably.
3. Resolve WPF launch failure dependency chain (`exit=-532462766`) and stabilize smoke probe.
4. Capture quantitative before/after metrics for refresh-throttle (`LastFrameMs`, UI update cadence, logs-tab active/inactive impact).

## Latest Evidence Snapshot (2026-03-05, follow-up execution)

- `publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi` rerun x2:
  - `run3`: `2026-03-05T18:14:40.0950026+09:00`
  - `run4`: `2026-03-05T18:15:58.0618412+09:00`
  - both runs:
    - WPF publish: FAIL (`NU1301` restore/network path)
    - WinUI preflight: PASS
    - WinUI publish: FAIL (`NU1301`)
    - diagnostics collected with profile outputs
- manifest rerun diff:
  - `build/reports/winui_manifest_diff_run3_vs_run4.txt`
  - result: all tracked fields `SAME`
- WPF launch smoke direct rerun:
  - `runA`: `2026-03-05T18:17:49.3166582+09:00`
  - `runB`: `2026-03-05T18:18:02.8195550+09:00`
  - result: `FAIL`, `ExitCode=-532462766`

## Artifact Contract

- `build/reports/host_publish_latest.txt`
- `build/reports/wpf_launch_smoke_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build.binlog`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_managed_diag.log`
- `build/reports/winui/obj-dump/**`
