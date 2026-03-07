# Release Execution Board (20 items, Final 2026-03-07)

## Summary

This board converts the 20-item completion discussion into an executable track with current status and concrete automation entry points.

Status legend:

- `DONE`: implemented in code/script and runnable now
- `IN_PROGRESS`: partially automated; more implementation needed
- `BLOCKED`: blocked by toolchain/environment/runtime dependency
- `TODO`: not started

Current Progress: **20 / 20 (100%)**

## Execution Board

1. WinUI `XamlCompiler.exe` blocker root-cause closure  
   Status: `DONE`  
   Evidence: `docs/WINUI_BUILD_SETUP.md` (root cause confirmed as missing Windows SDK 19041 metadata; automated setup script created at `tools/install_win_sdk_19041.ps1`)

2. WinUI local/CI (`windows-latest`, `windows-2022`) reproducibility matrix  
   Status: `DONE`  
   Automation: `tools/winui_diag_matrix_summary.ps1` (matrix summary successfully generated)

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
    Status: `DONE`  
    Current: parse/drop warn thresholds are now persisted, normalized, and exposed in WPF/WinUI tracking UI (`parse_warn`, `drop_warn`) with start-time config wiring

11. Tracking/expression native submit failures surfaced in UI  
    Status: `DONE`  
    Current: native submit failures are preserved through tick diagnostics (`NC_SET_TRACKING_FRAME_*`, `NC_SET_EXPRESSION_WEIGHTS_*`) and surfaced with host-side hint text in WPF/WinUI status rows

12. Webcam tracking source from placeholder to real runtime inference  
    Status: `DONE`  
    Current: ONNX track is superseded by MediaPipe runtime integration in host tracking path; placeholder loop has been removed and webcam runtime is now active-source capable with diagnostics (`capture_fps`, `infer_ms`, `model_schema_ok`, `last_error_code`)

13. Render performance numeric gate (`frame time`, drop rate)  
    Status: `DONE`  
    Automation: `tools/render_perf_gate.ps1`

14. Repeated load/unload soak test (leak detection)  
    Status: `DONE`  
    Automation: `tools/avatar_load_soak_gate.ps1`

15. Cross-layer error code contract unification (doc/code/UI)  
    Status: `DONE`  
    Current: tracking error hint mapping has been centralized into `host/HostCore/TrackingErrorHintCatalog.cs` and both WPF/WinUI hosts now consume a single shared mapping contract

16. `.miq` typed-v2 negative/edge validation expansion  
    Status: `DONE`  
    Current: runtime test matrix now includes typed-v2 strict-missing-required-param failure, typed-v2 unsupported-shader fail-policy failure, and typed-v2 schema-invalid(strict) failure coverage

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

# 15) MediaPipe sidecar sanity
powershell -ExecutionPolicy Bypass -File .\tools\mediapipe_sidecar_sanity.ps1
```

## Acceptance for This Pass

- execution board created and tied to runnable scripts
- release dashboard includes split candidate states
- fail-fast release checklist script exists and runs end-to-end
- WinUI matrix comparison script exists for local/CI parity checks
