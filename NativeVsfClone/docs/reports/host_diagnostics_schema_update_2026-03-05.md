# Host Diagnostics Schema Update (2026-03-05)

## Summary

This update hardens WinUI host diagnostics so toolchain blockers are decision-complete from manifest data alone and downstream host-track resolution remains backward compatible.

Key outcomes:

- Added a new `preflight_probe` section to WinUI diagnostic manifest output.
- Preserved existing `preflight` shape to avoid breaking existing consumers.
- Added explicit probe evidence for:
  - `.NET 8 SDK` detection
  - `Visual Studio discovery` detection
  - `Windows SDK 10.0.19041 metadata` path checks
- Adjusted failure classification evaluation order:
  - precondition failures first
  - managed `WMC9999` (`TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`) before generic `XAML_COMPILER_EXEC_FAIL`

## Scope

In scope:

- `tools/publish_hosts.ps1` diagnostics and classification behavior
- documentation updates in `README.md` and `CHANGELOG.md`
- execution verification with host publish + quality gates

Out of scope:

- toolchain installation/remediation itself (Windows SDK metadata still missing in current environment)
- runtime parity smoke checklist execution (still blocked by WinUI publish precondition failure)

## Implementation Details

### 1) Manifest schema extension

Updated `Write-WinUiDiagnosticManifest` to emit:

- `preflight` (legacy, unchanged fields only)
  - `passed`
  - `failed_checks`
  - `detected_sdks`
  - `recommended_actions`
- `preflight_probe` (new structured evidence)
  - per-check `detected` result
  - probe evidence and inspected paths

### 2) Windows SDK metadata probe evidence

Preflight now checks multiple metadata candidates and stores the inspected path list in the manifest:

- `C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.19041.0\Facade\Windows.winmd`
- `C:\Program Files (x86)\Windows Kits\10\References\10.0.19041.0\Windows.Foundation.FoundationContract\3.0.0.0\Windows.Foundation.FoundationContract.winmd`

If none are found:

- failed check: `MISSING_WINDOWS_SDK_19041_METADATA`
- failure class path stays under precondition failure contract

### 3) Failure class precedence

Classification logic now explicitly follows this order:

1. preflight/toolchain precondition failure
2. managed XAML `WMC9999` (`TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`)
3. diagnostics-pattern classes (`NUGET_SOURCE_UNREACHABLE`, `XAML_COMPILER_EXEC_FAIL`, etc.)
4. `UNKNOWN`

This prevents managed platform-unsupported signals from being masked by generic XAML execution errors when preflight has passed.

## Validation

Executed commands:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1
```

Observed results:

- WPF publish: PASS
- WinUI preflight: FAIL (`MISSING_WINDOWS_SDK_19041_METADATA`)
- Manifest class: `TOOLCHAIN_PRECONDITION_FAILED`
- Gate HostTrack:
  - `HostTrackStatus=BLOCKED_TOOLCHAIN_PRECONDITION`
  - reason resolved from manifest failure class
- Quality baseline: PASS

## Artifacts

- `build/reports/host_publish_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_managed_diag.log`
- `build/reports/quality_baseline_summary.txt`
- `build/reports/vsfavatar_gate_summary.txt`

## Remaining Blocker

Current environment still lacks Windows SDK metadata/facade requirements for WinUI net8 toolchain preflight:

- blocker code: `MISSING_WINDOWS_SDK_19041_METADATA`
- host-track status: `BLOCKED_TOOLCHAIN_PRECONDITION`

Next unblock action is environment remediation for Windows SDK 10.0.19041 metadata components.

## Re-validation Round (2026-03-05 02:35 KST)

Re-ran the full planned verification sequence after upstream push to confirm the contract remains stable in the same environment.

Executed:

```powershell
dotnet --version
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1
```

Observed:

- `dotnet --version`: `8.0.418`
- WPF publish: PASS
- WinUI preflight: FAIL (`MISSING_WINDOWS_SDK_19041_METADATA`)
- diagnostics manifest class: `TOOLCHAIN_PRECONDITION_FAILED`
- HostTrackStatus: `BLOCKED_TOOLCHAIN_PRECONDITION`
- quality baseline summary: `Overall: PASS`

Conclusion:

- no behavioral regression after diagnostics/schema hardening changes
- blocker remains environment/toolchain precondition, not parser or gate logic
