# Host WinUI Diagnostics Profile + WPF Launch Smoke Automation (2026-03-05)

## Summary

This update closes the planned host-track implementation slice for deterministic diagnostics and evidence capture.

Implemented outcomes:

- WinUI diagnostics now run as explicit profiles and are persisted as structured manifest entries.
- WinUI preflight now validates additional toolchain/package prerequisites and reports probe evidence.
- WPF non-interactive launch smoke is automated and writes a reusable report artifact.
- CI WinUI diagnostics now run on an OS matrix to compare environment-dependent failures.
- A single blocker status board is published for open/closed/next-action tracking.

## Scope

- `tools/publish_hosts.ps1`
- `tools/wpf_launch_smoke.ps1` (new)
- `.github/workflows/host-publish.yml`
- `docs/reports/host_blocker_status_board_2026-03-05.md` (new)
- report link updates in:
  - `docs/reports/wpf_ui_smoke_and_perf_2026-03-05.md`
  - `docs/reports/wpf_verification_roundup_2026-03-05.md`
  - `docs/reports/winui_ui_refresh_throttle_parity_2026-03-05.md`

## Detailed Changes

### 1) `tools/publish_hosts.ps1`

Added new execution controls:

- `WinUiDiagnosticsProfile` (`full|diag-only`, default `full`)
- `RunWpfLaunchSmoke` (default `true`)
- `WpfLaunchSmokeFailOnError` (default `false`)
- `WpfLaunchSmokeDurationSeconds` (default `6`)
- `WpfLaunchSmokeReportPath` (default `.\build\reports\wpf_launch_smoke_latest.txt`)

WinUI diagnostics changes:

- `winui_diagnostic_manifest.json` now includes `profiles[]`:
  - profile name
  - enabled flag
  - command
  - exit code
  - artifacts
  - root-cause hints (or skipped reason)
- `diag-default` profile captures the standard diagnostic build path.
- `managed-xaml` profile captures the managed compiler path (`UseXamlCompilerExecutable=false`) when enabled.

Preflight probe expansion:

- existing checks kept:
  - `DOTNET_8_SDK`
  - `VISUAL_STUDIO_DISCOVERY`
  - `WINDOWS_SDK_19041_METADATA`
- added checks:
  - `MSBUILD_DISCOVERY`
  - `WINDOWS_SDK_19041_BINTOOLS` (`rc.exe` probe)
  - `WINDOWSAPPSDK_PACKAGE_CACHE` (package presence by project-pinned version)

Failure class mapping additions:

- `TOOLCHAIN_WINDOWS_SDK_INCOMPLETE`
- `WINDOWSAPPSDK_RESTORE_INCOMPLETE`
- `TOOLCHAIN_VISUAL_STUDIO_INCOMPLETE`

WPF smoke integration:

- after WPF publish success, `tools/wpf_launch_smoke.ps1` is invoked (configurable).
- result is logged to `build/reports/host_publish_latest.txt`.

### 2) `tools/wpf_launch_smoke.ps1` (new)

Behavior:

- launches target EXE in a specific working directory
- waits for configurable alive window
- marks `PASS` if process remains alive during probe window
- marks `FAIL` with exit code when process exits early
- captures recent `.NET Runtime` event `1026` entries from Application log
- writes `build/reports/wpf_launch_smoke_latest.txt`
- optional fail-fast mode via `TreatFailureAsError`

### 3) `.github/workflows/host-publish.yml`

Workflow changes:

- triggers now include `tools/wpf_launch_smoke.ps1` changes.
- WPF outputs upload now includes:
  - `build/reports/wpf_launch_smoke_latest.txt`
- WinUI diagnostics job now uses matrix:
  - `windows-latest`
  - `windows-2022`
- WinUI job adds diagnostic manifest existence validation step.
- WinUI artifacts upload contract now explicitly includes:
  - `build/reports/winui/winui_diagnostic_manifest.json`
  - `build/reports/winui/winui_build.binlog`
  - `build/reports/winui/winui_build_diag.log`
  - `build/reports/winui/winui_build_managed_diag.log`
  - `build/reports/winui/obj-dump/**`

## Artifact Contract (Current)

- `build/reports/host_publish_latest.txt`
- `build/reports/wpf_launch_smoke_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build.binlog`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_managed_diag.log`
- `build/reports/winui/obj-dump/**`

## Validation Performed in This Update

- PowerShell parse validation:
  - `tools/publish_hosts.ps1`: PASS
  - `tools/wpf_launch_smoke.ps1`: PASS
- smoke-script functional sanity check:
  - launched `notepad.exe` as probe target
  - report generated and returned `PASS`

## Open Follow-up Items

1. Run full local `publish_hosts.ps1 -IncludeWinUi` with new profile-enabled diagnostics and verify manifest profile outputs under actual WinUI failure conditions.
2. Compare CI matrix artifacts (`windows-latest` vs `windows-2022`) to isolate environment-specific blocker differences.
3. Resolve WPF `DllNotFoundException` dependency chain for headless launch-failure signature (`exit=-532462766`) and confirm smoke stability.

## Related Tracking

- `docs/reports/host_blocker_status_board_2026-03-05.md`
