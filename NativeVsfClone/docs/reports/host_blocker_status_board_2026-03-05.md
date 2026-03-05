# Host Blocker Status Board (2026-03-05)

## Summary

Single status board for host-track closure work across WPF-first release gating and WinUI diagnostics recovery.

## Open Blockers

1. WinUI compile blocker remains open.
   - current local signature: `XamlCompiler.exe` publish-stage failure (`MSB3073`)
   - current class: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
   - latest evidence: `build/reports/winui/winui_diagnostic_manifest.json`

## Closed Items

1. WPF-first publish/gate baseline is stable.
   - `publish_hosts.ps1` WPF publish path: PASS in latest rounds
   - `vsfavatar_quality_gate.ps1 -UseFixedSet`: PASS (`HostTrackStatus=PASS_WPF_BASELINE`)
   - `run_quality_baseline.ps1`: PASS

2. WinUI diagnostics collection contract is deterministic.
   - preflight probe + failure class + binlog/log artifacts produced on failure path
   - profile-based diagnostics (`diag-default`, `managed-xaml`) persisted in manifest
  - local rerun comparison (`runA` vs `runB`) shows no class/profile drift:
    - `failure_class=TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED` (same)
    - profiles exit/hints (same)
    - evidence: `build/reports/winui_manifest_diff_runA_vs_runB.txt`

3. WPF non-interactive launch smoke is stabilized.
   - latest evidence: `build/reports/wpf_launch_smoke_latest.txt`
   - latest status: `PASS`, `ExitCode=0`
   - closure change:
     - startup null-guard + UI-ready gating added in `host/WpfHost/MainWindow.xaml.cs`
     - smoke report now includes scoped event-log window and probe-path inventory (`tools/wpf_launch_smoke.ps1`)

## Next Actions

1. Compare WinUI diagnostics across CI matrix (`windows-latest`, `windows-2022`) and local run using `tools/compare_winui_diag_manifest.ps1`.
2. Resolve WinUI `XamlCompiler.exe` publish-stage blocker (`MSB3073`/`WMC9999`) and validate whether auth-related feed hints (`401/403`) are causal or secondary.
3. Validate CI matrix parity via uploaded per-OS manifest summary (`winui_manifest_summary_*.txt`) and local manifest diff.
4. Capture quantitative before/after metrics for refresh-throttle (`LastFrameMs`, UI update cadence, logs-tab active/inactive impact).

## Latest Evidence Snapshot (2026-03-05, implementation follow-up)

- `publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi` rerun x2:
  - `runA`: `2026-03-05T21:30:53+09:00`
  - `runB`: `2026-03-05T21:31:50+09:00`
  - both runs:
    - WPF publish: PASS
    - WPF launch smoke: PASS after startup null-guard fix (`latest direct smoke run: 2026-03-05T21:36:16+09:00`)
    - WinUI preflight: PASS
    - WinUI publish: FAIL (`XamlCompiler.exe`/`MSB3073`)
    - diagnostics collected with profile outputs
- manifest rerun diff:
  - `build/reports/winui_manifest_diff_runA_vs_runB.txt`
  - result: all tracked fields `SAME`
- latest WinUI manifest key fields:
  - `failure_class=TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
  - `root_cause_hints` includes `MSB3073`, `WMC9999`, and auth-related feed hint (`401/403`)

## Artifact Contract

- `build/reports/host_publish_latest.txt`
- `build/reports/wpf_launch_smoke_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build.binlog`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_managed_diag.log`
- `build/reports/winui/obj-dump/**`

## Detailed Implementation Record

- `docs/reports/host_blocker_closure_implementation_pass_2026-03-05.md`
