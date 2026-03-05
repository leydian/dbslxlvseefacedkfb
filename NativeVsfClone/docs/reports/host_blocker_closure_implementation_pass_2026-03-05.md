# Host Blocker Closure Implementation Pass (2026-03-05)

## Summary

This document captures the detailed implementation and verification outcomes for the blocker-closure pass executed on 2026-03-05.

Primary outcomes:

1. WPF non-interactive launch smoke was stabilized in local/headless execution.
2. WinUI diagnostics were hardened with richer classification evidence and deterministic rerun comparison.
3. CI diagnostics output was upgraded with per-OS manifest summary artifacts.

## Scope

Changed code/scripts/workflow:

- `tools/publish_hosts.ps1`
- `tools/wpf_launch_smoke.ps1`
- `tools/compare_winui_diag_manifest.ps1`
- `.github/workflows/host-publish.yml`
- `host/WpfHost/MainWindow.xaml.cs`
- `CMakeLists.txt`

Changed reporting docs:

- `docs/reports/host_blocker_status_board_2026-03-05.md`
- `docs/reports/host_winui_diag_profile_and_wpf_smoke_2026-03-05.md`
- `docs/reports/wpf_ui_smoke_and_perf_2026-03-05.md`

## Detailed Changes

### 1) `tools/publish_hosts.ps1`

Added operational controls and diagnostics hardening:

- New options:
  - `WinUiRestoreRetryCount` (default `1`)
  - `NuGetProbeTimeoutSeconds` (default `8`)
- Added `Invoke-DotNetCommandWithRetry`:
  - retries `dotnet publish` path on `NU1301`-like transient restore failures.
- Added `Get-NuGetSourceProbe`:
  - captures current NuGet source/proxy state into diagnostics payload.
  - records per-source enabled/reachability status.
- Manifest contract expanded:
  - new `nuget_probe` section included in `winui_diagnostic_manifest.json`.
- Root-cause hints expanded:
  - detects auth-like signals (`401/403`) in diagnostics logs.
- Failure class mapping expanded:
  - adds `NUGET_AUTH_FAILURE` class.

### 2) `tools/wpf_launch_smoke.ps1`

Improved smoke report precision and dependency triage:

- Added `AdditionalProbePaths`:
  - probe paths are prepended to process PATH for launch-time dependency lookup.
- Added `IncludeDirectoryInventory`:
  - captures DLL inventory from probe directories.
- Added dependency hint extraction:
  - derives likely faulting module/dependency names from event text.
- Narrowed event capture window:
  - event query now starts at smoke run boundary to avoid stale historical entries.

### 3) `tools/compare_winui_diag_manifest.ps1`

Drift comparison coverage expanded:

- `preflight.warnings`
- `nuget_probe.summary.enabled/reachable/unreachable/unknown`
- `profiles[].command`

### 4) `.github/workflows/host-publish.yml`

Diagnostics matrix evidence strengthened:

- workflow trigger paths include `tools/compare_winui_diag_manifest.ps1`.
- WinUI matrix job adds `Summarize WinUI Diagnostic Manifest` step:
  - outputs `build/reports/winui_manifest_summary_<os>.txt`
  - appends key fields to job step summary.
- summary artifacts uploaded together with existing WinUI diagnostics contract files.

### 5) `host/WpfHost/MainWindow.xaml.cs`

Startup crash hardening:

- Added `_uiReady` guard to skip early `TextChanged` events during initial XAML setup.
- Added null guards around validation controls inside `RefreshValidationState()`.
- Result: startup-time `NullReferenceException` in validation path no longer reproduces in local smoke runs.

### 6) `CMakeLists.txt`

Runtime dependency hardening for `nativecore` host loading:

- Added MSVC runtime policy:
  - `CMAKE_MSVC_RUNTIME_LIBRARY` = `MultiThreaded$<$<CONFIG:Debug>:Debug>`

## Verification Executed

### Script parse validation

- `tools/publish_hosts.ps1`: PASS
- `tools/wpf_launch_smoke.ps1`: PASS
- `tools/compare_winui_diag_manifest.ps1`: PASS

### Build validation

- `cmake --build .\build --config Release --target nativecore`: PASS

### Host publish/diagnostics reruns

Executed twice:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi -WinUiRestoreRetryCount 1 -NuGetProbeTimeoutSeconds 6
```

Observed:

- WPF publish: PASS
- WPF launch smoke: PASS (latest direct rerun status confirmed separately)
- WinUI preflight: PASS
- WinUI publish: FAIL (`XamlCompiler.exe` / `MSB3073`)
- WinUI failure class: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`

### Manifest determinism check

Executed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\compare_winui_diag_manifest.ps1 `
  -BaseManifestPath .\build\reports\winui\winui_diagnostic_manifest_runA.json `
  -TargetManifestPath .\build\reports\winui\winui_diagnostic_manifest_runB.json `
  -OutputPath .\build\reports\winui_manifest_diff_runA_vs_runB.txt
```

Result:

- all tracked fields: `SAME`
- local classification path remains deterministic for the compared runs.

### Direct WPF smoke rerun

Executed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\wpf_launch_smoke.ps1 `
  -ExePath .\dist\wpf\WpfHost.exe `
  -WorkingDirectory .\dist\wpf `
  -AliveSeconds 6 `
  -ReportPath .\build\reports\wpf_launch_smoke_latest.txt `
  -AdditionalProbePaths .\build\Release
```

Result:

- `Status: PASS`
- `ExitCode: 0`

## Current Blocker State (Post-pass)

Closed:

- WPF headless launch smoke instability (local environment baseline in this pass).

Open:

- WinUI publish-stage compiler blocker:
  - signature: `XamlCompiler.exe` path (`MSB3073`), with managed diagnostics hinting `WMC9999`.
  - current class: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`

## Artifact References

- `build/reports/host_publish_latest.txt`
- `build/reports/wpf_launch_smoke_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui_manifest_diff_runA_vs_runB.txt`
- `build/reports/winui_manifest_summary_*.txt` (CI matrix output)
