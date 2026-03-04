# Host Plan Execution Update (2026-03-04)

## Summary

This update implements the agreed action plan after the prior WinUI publish blocker triage round.
The main objective was to make host publish failure diagnosis deterministic, move HostTrack status to evidence-based resolution, and enforce baseline + host checks consistently in CI.

## Scope Completed

1. WinUI preflight fail-fast policy in `tools/publish_hosts.ps1`
2. Normalized WinUI failure class output in diagnostic manifest
3. Evidence-based HostTrack auto-resolution for VSFAvatar gate scripts
4. CI integration for baseline-first host publish (`pull_request` + `push(main)`)
5. Manual runtime parity smoke checklist formalization

## Detailed Implementation

### 1) WinUI preflight fail-fast (`tools/publish_hosts.ps1`)

Added preflight checks before WinUI publish starts:

- `.NET 8 SDK` presence from `dotnet --list-sdks`
- Visual Studio discovery availability via `vswhere`

Behavior:

- if checks fail, WinUI publish does not start
- script writes explicit preflight failure lines to `build/reports/host_publish_latest.txt`
- diagnostics collection still runs (unless disabled) for traceability

Added preflight structure in manifest:

- `preflight.passed`
- `preflight.failed_checks[]`
- `preflight.detected_sdks[]`
- `preflight.recommended_actions[]`

### 2) Failure class normalization (`tools/publish_hosts.ps1`)

Added normalized classification output to WinUI diagnostic manifest:

- `failure_class`
- `failure_class_confidence`

Current class set:

- `TOOLCHAIN_MISSING_DOTNET8`
- `TOOLCHAIN_PRECONDITION_FAILED`
- `NUGET_SOURCE_UNREACHABLE`
- `MANAGED_XAML_TASK_MISSING_DEP`
- `XAML_COMPILER_EXEC_FAIL`
- `UNKNOWN`

Classification source combines:

- preflight result
- standard diagnostic log patterns (`NU1101`, `NU1301`, `MSB3073`, `XamlCompiler.exe`)
- managed fallback diagnostic log patterns (`System.Security.Permissions`, `WMC9999`)

### 3) HostTrack auto-resolution (`tools/vsfavatar_quality_gate.ps1`, `tools/vsfavatar_sample_report.ps1`)

Changed default host-track behavior from static blocker value to automatic resolution:

- `HostTrackStatus` default: `AUTO`

Resolution order:

1. if both `dist/wpf/WpfHost.exe` and `dist/winui/WinUiHost.exe` exist -> `PASS`
2. else if WinUI diagnostic manifest exists -> map `failure_class` to blocked/fail status
3. else if host publish report exists -> infer from `WinUI publish: failed`/`WinUI exe:` lines
4. else -> `UNKNOWN`

Added report metadata:

- `HostTrackStatusReason`
- `HostTrackEvidencePath`

Host DoD rule updated:

- `HostTrack_DoD=PASS` only when `HostTrackStatus=PASS`

### 4) CI integration (`.github/workflows/host-publish.yml`)

Added a new `quality-baseline` job:

- configure + build
- run `tools/run_quality_baseline.ps1`
- upload baseline reports

Updated `publish-hosts` job:

- now depends on `quality-baseline`

Expanded workflow trigger paths to include baseline/gate scripts so CI re-runs when gate logic changes.

### 5) Manual parity checklist formalization

Added:

- `docs/reports/host_runtime_parity_smoke_checklist_2026-03-04.md`

Checklist includes 8 parity scenarios across WPF/WinUI, expected outcomes, and a result template table.

## Documentation Synchronization

Updated:

- `README.md` (preflight fail-fast and manifest schema notes, HostTrack DoD wording)
- `docs/INDEX.md` (new report links)
- `CHANGELOG.md` (implementation record for this update)

## Verification Performed

Static script parse validation:

- `tools/publish_hosts.ps1`: parse OK
- `tools/vsfavatar_quality_gate.ps1`: parse OK
- `tools/vsfavatar_sample_report.ps1`: parse OK

## Follow-up

Operational verification should now run in order:

1. `tools/run_quality_baseline.ps1`
2. `tools/publish_hosts.ps1 -IncludeWinUi`
3. If WinUI succeeds, execute manual parity smoke checklist
