# Release Execution Board (20 items, 2026-03-06)

## Summary

This board converts the 20-item completion discussion into an executable track with current status and concrete automation entry points.

Status legend:

- `DONE`: implemented in code/script and runnable now
- `IN_PROGRESS`: partially automated; more implementation needed
- `BLOCKED`: blocked by toolchain/environment/runtime dependency
- `TODO`: not started

## Execution Board

1. WinUI `XamlCompiler.exe` blocker root-cause closure  
   Status: `BLOCKED`  
   Evidence: `build/reports/winui/winui_diagnostic_manifest.json`

2. WinUI local/CI (`windows-latest`, `windows-2022`) reproducibility matrix  
   Status: `IN_PROGRESS`  
   Automation: `tools/winui_diag_matrix_summary.ps1`, `tools/winui_xaml_min_repro.ps1`

3. `publish_hosts.ps1` WinUI failure classification granularity  
   Status: `DONE`  
   Automation: `tools/publish_hosts.ps1` (`failure_class`, `root_cause_hints`, `profiles`, `preflight_probe`)

4. NuGet outage fallback strategy (cache/mirror)  
   Status: `DONE`  
   Automation: `tools/nuget_mirror_bootstrap.ps1` (local mirror source bootstrap)

5. `vsfavatar_sidecar.exe` lock (`LNK1104`) recurrence prevention  
   Status: `DONE`  
   Automation: `tools/sidecar_lock_guard.ps1`

6. WPF/WinUI shared E2E scenario automation  
   Status: `DONE`  
   Automation: `tools/host_e2e_gate.ps1` (WPF-first + optional WinUI)

7. Gate sample strategy split (fixed + large real-world set)  
   Status: `DONE`  
   Automation: `tools/sample_profiles/*`, `tools/sample_profile_resolve.ps1`

8. VSFAvatar GateD pass-rate trend tracking  
   Status: `DONE`  
   Automation: `tools/vsfavatar_gated_trend.ps1`

9. Tracking parser fuzz tests (`format-a`/`format-b`)  
   Status: `DONE`  
   Automation: `tools/tracking_parser_fuzz_gate.ps1`

10. Tracking threshold config externalization  
    Status: `IN_PROGRESS`  
    Current: stale timeout/fps cap persistence exists; parse/drop threshold contract remains

11. Tracking/expression native submit failures surfaced in UI  
    Status: `IN_PROGRESS`  
    Current: diagnostics/log path exists; dedicated error-code surface polish pending

12. Webcam ONNX source from placeholder to real inference  
    Status: `TODO`

13. Render performance numeric gate (`frame time`, drop rate)  
    Status: `DONE`  
    Automation: `tools/render_perf_gate.ps1`

14. Repeated load/unload soak test (leak detection)  
    Status: `DONE`  
    Automation: `tools/avatar_load_soak_gate.ps1`

15. Cross-layer error code contract unification (doc/code/UI)  
    Status: `IN_PROGRESS`  
    Current: major avatar gates aligned; final host UI wording/code mapping remains

16. `.xav2` typed-v2 negative/edge validation expansion  
    Status: `IN_PROGRESS`  
    Current: core parser/runtime tests exist; edge matrix expansion pending

17. Session schema migration regression (`v1/v2/v3 -> v4`)  
    Status: `DONE`  
    Automation: `tools/session_state_migration_check.ps1`

18. Diagnostics bundle enrichment (repro command + env snapshot)  
    Status: `DONE`  
    Implementation: `host/HostCore/HostController.MvpFeatures.cs` (`repro_commands.txt`, `environment_snapshot.json`)

19. Dashboard split: `WPF-only PASS` vs `Full PASS`  
    Status: `DONE`  
    Automation: `tools/release_gate_dashboard.ps1` (new `ReleaseCandidateWpfOnly`, `ReleaseCandidateFull`)

20. Release checklist fail-fast enforcement script  
    Status: `DONE`  
    Automation: `tools/release_readiness_gate.ps1`

## New/Updated Commands

```powershell
# 1) Full fail-fast release gate (WPF-first)
powershell -ExecutionPolicy Bypass -File .\tools\release_readiness_gate.ps1

# 2) Full gate including WinUI track (will fail-fast on WinUI blocker)
powershell -ExecutionPolicy Bypass -File .\tools\release_readiness_gate.ps1 -IncludeWinUi

# 3) Refresh dashboard with split release-candidate states
powershell -ExecutionPolicy Bypass -File .\tools\release_gate_dashboard.ps1

# 4) Compare multiple WinUI diagnostic manifests (local + CI artifacts)
powershell -ExecutionPolicy Bypass -File .\tools\winui_diag_matrix_summary.ps1 `
  -ManifestPaths @(
    ".\build\reports\winui\winui_diagnostic_manifest_local.json",
    ".\build\reports\winui\winui_diagnostic_manifest_windows-latest.json",
    ".\build\reports\winui\winui_diagnostic_manifest_windows-2022.json"
  )

# 5) VSFAvatar GateD trend summary
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_gated_trend.ps1

# 6) Tracking parser fuzz gate
powershell -ExecutionPolicy Bypass -File .\tools\tracking_parser_fuzz_gate.ps1

# 7) Session schema migration check
powershell -ExecutionPolicy Bypass -File .\tools\session_state_migration_check.ps1

# 8) Avatar load soak gate
powershell -ExecutionPolicy Bypass -File .\tools\avatar_load_soak_gate.ps1 -IterationsPerSample 10

# 9) Render performance numeric gate (metrics csv required)
powershell -ExecutionPolicy Bypass -File .\tools\render_perf_gate.ps1 -MetricsCsvPath ".\build\reports\metrics_latest.csv"

# 10) NuGet mirror bootstrap
powershell -ExecutionPolicy Bypass -File .\tools\nuget_mirror_bootstrap.ps1

# 11) Sidecar lock guard
powershell -ExecutionPolicy Bypass -File .\tools\sidecar_lock_guard.ps1

# 12) Host end-to-end gate
powershell -ExecutionPolicy Bypass -File .\tools\host_e2e_gate.ps1 -SkipNativeBuild -NoRestore

# 13) WinUI minimal XAML repro
powershell -ExecutionPolicy Bypass -File .\tools\winui_xaml_min_repro.ps1 -NoRestore

# 14) Resolve sample profile
powershell -ExecutionPolicy Bypass -File .\tools\sample_profile_resolve.ps1 -Profile fixed_set
```

## Acceptance for This Pass

- execution board created and tied to runnable scripts
- release dashboard includes split candidate states
- fail-fast release checklist script exists and runs end-to-end
- WinUI matrix comparison script exists for local/CI parity checks
