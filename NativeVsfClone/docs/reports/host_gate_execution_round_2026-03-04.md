# Host/Gate Execution Round Report (2026-03-04)

## Summary

This round executed the post-plan validation sequence end-to-end:

- quality gates were re-run and baseline status was re-confirmed
- host publish pipeline was re-run with WPF/WinUI split verification
- WinUI XAML failure diagnostics were expanded for actionable root-cause extraction

Result:

- gate tracks: PASS
- host publish:
  - WPF: PASS
  - WinUI: FAIL (`XamlCompiler.exe` exit code `1`)

## Detailed Change Summary

### 1) Publish diagnostics pipeline hardening (`tools/publish_hosts.ps1`)

- Added a second-stage managed XAML diagnostic execution path after WinUI publish failure.
- Added manifest fields for:
  - managed diagnostic command
  - managed diagnostic exit code
  - managed diagnostic log paths
  - extracted root-cause hints
- Added root-cause hint extraction rules:
  - `MSB3073` + `XamlCompiler.exe` path detection
  - `NU1101` / `NU1301` NuGet failures
  - `System.Security.Permissions` missing dependency in managed task load path
- Expanded copied WinUI obj diagnostics to include `input.json` for reproduction parity.

### 2) Documentation synchronization

- `README.md`:
  - inserted latest validation snapshot for 2026-03-04
  - documented new WinUI managed diagnostic artifacts
- `CHANGELOG.md`:
  - added dated entry with implementation and verification command set
- `docs/INDEX.md`:
  - registered this report for traceability

### 3) Baseline refresh artifacts

- Re-generated tracked VRM fixed profile outputs:
  - `build/reports/vrm_probe_fixed5.txt`
  - `build/reports/vrm_gate_fixed5.txt`
- These files now reflect the latest gate execution timestamp and current output schema fields.

## Executed Commands

```powershell
dotnet --info
dotnet --list-sdks

powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5
powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick

powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi
```

Additional diagnostic command used to isolate WinUI cause:

```powershell
dotnet publish host/WinUiHost/WinUiHost.csproj -c Release -r win-x64 --self-contained true -p:Platform=x64 /p:PublishSingleFile=false /p:PublishTrimmed=false /p:WindowsAppSDKSelfContained=true /p:UseXamlCompilerExecutable=false -o dist/winui_test_managed_xaml
```

## Gate Results

### VSFAvatar (`vsfavatar_quality_gate.ps1 -UseFixedSet`)

- GateA/B/C/D: PASS
- Overall: PASS
- ParserTrack_DoD: PASS
- HostTrackStatus: `BLOCKED_XAML_COMPILER`
- Artifact:
  - `build/reports/vsfavatar_probe_latest_after_gate.txt`
  - `build/reports/vsfavatar_gate_summary.txt`
  - `build/reports/vsfavatar_gate_aggregate.csv`
  - `build/reports/vsfavatar_gate_aggregate.txt`

### VRM (`vrm_quality_gate.ps1 -Profile fixed5`)

- GateA/B/C/D: PASS
- Overall: PASS
- Artifact:
  - `build/reports/vrm_probe_fixed5.txt`
  - `build/reports/vrm_gate_fixed5.txt`

### VXAvatar/VXA2/XAV2 (`vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`)

- GateA/B/C/D/E/F/G/H: PASS
- Overall: PASS
- Artifact:
  - `build/reports/vxavatar_probe_latest.txt`
  - `build/reports/vxavatar_gate_summary.txt`
  - `build/reports/vxavatar_gate_summary.json`

## Host Publish Results

### WPF

- `publish_hosts.ps1 -IncludeWinUi` WPF stage: PASS
- Output confirmed:
  - `dist/wpf/WpfHost.exe`
  - `dist/wpf/nativecore.dll`

### WinUI

- `publish_hosts.ps1 -IncludeWinUi` WinUI stage: FAIL
- Failing command path:
  - `XamlCompiler.exe ... input.json output.json` -> exit code `1`
- Managed fallback diagnostic (`UseXamlCompilerExecutable=false`) surfaced:
  - `MSB4062`
  - missing assembly: `System.Security.Permissions, Version=6.0.0.0`

## Script hardening added in this round

Updated `tools/publish_hosts.ps1`:

- added `CollectManagedXamlDiagnostics` option (default `true`)
- on WinUI publish failure, runs an additional managed XAML compile diagnostic pass
- captures managed diagnostic logs:
  - `build/reports/winui/winui_build_managed_diag.log`
  - `build/reports/winui/winui_build_managed_stderr.log`
- extends manifest with:
  - managed diagnostic command + exit code
  - extracted root-cause hints
- expands obj-dump collection to include `input.json` in addition to `output.json`/log files

## Current Blocker

- WinUI publish remains blocked by XAML compile toolchain failure.
- Actionable hint now captured automatically:
  - managed task load path indicates missing `System.Security.Permissions` dependency.

## Manual Host Smoke Status

Not executed in this round.

Reason:

- plan requires both WPF/WinUI parity validation after successful WinUI publish
- WinUI publish is still blocked, so parity runtime smoke is deferred

## Artifacts

- `build/reports/host_publish_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build.binlog`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_stderr.log`
- `build/reports/winui/winui_build_managed_diag.log`
- `build/reports/winui/winui_build_managed_stderr.log`
