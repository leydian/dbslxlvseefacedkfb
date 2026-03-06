# R01-R20 Top5 Implementation + WinUI SDK Contract Execution (2026-03-05)

## Summary

Executed the approved Top5 scope (`R01/R02/R03/R05/R13`) with production-focused contract upgrades in `HostCore` and host UIs (WPF/WinUI), then reran host publish diagnostics with WinUI blocker-first sequencing.

Result snapshot:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL (same `MSB3073` / `XamlCompiler.exe`, classified as `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`)

## Implemented Changes

### 1) HostCore Top5 contract hardening

Updated files:

- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.MvpFeatures.cs`

Key changes:

- `R01` preflight contract expansion:
  - `PreflightCheckResult` now includes `CheckCode`.
  - Added checks for:
    - `LOCAL_APPDATA_WRITABLE`
    - `DIAGNOSTICS_OUTPUT_WRITABLE`
    - `SIDECAR_PATH_VALID`
  - Existing checks now emit stable check-code identifiers for UI/diagnostics mapping.
- `R02` import flow contract:
  - Added `ImportPlan` type (`Route`, `IsSupported`, `Guidance`, `Fallback`).
  - Added `BuildImportPlan(path)` and kept `BuildImportGuidance(path)` as formatted adapter.
- `R03` user-facing error contract:
  - `UserFacingError` now includes stable `ErrorCode`.
  - Error guidance now includes code prefix (`[ERR_...] ...`).
- `R13` async load completion semantics:
  - `LoadProgressState` now includes `OperationId`.
  - Added operation-id based stale completion guard (late completion from canceled/superseded load no longer overwrites active progress state).
  - `CancelLoadAvatar()` publishes cancel intent for the active operation id.
- `R05` auto-quality policy and recovery:
  - `AutoQualityPolicy` expanded with recovery controls:
    - `RecoveryFrameMsThreshold`
    - `RecoveryConsecutiveFrameLimit`
  - Runtime guard now supports downgrade-to-performance and recovery-to-quality under stable frame window.

### 2) WPF/WinUI UI parity updates for new contracts

Updated files:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- Preflight dialogs now display `CheckCode` for each check.
- Load guidance now consumes `BuildImportPlan(...)` (guidance + fallback text).
- Load progress text now includes `OperationId`.
- Auto-quality policy UI now includes recovery inputs (`Recovery ms`, `Recovery n`) and applies full 5-field policy.
- Session-default hydration includes recovery policy fields.

### 3) WinUI blocker-first script hardening

Updated file:

- `tools/publish_hosts.ps1`

Changes:

- Added repo-root SDK contract probe (`Get-DotNetVersionInfo`) and enforced `dotnet` major `8` when `-IncludeWinUi` is enabled.
- Dotnet invocations now execute from repo root context in command helpers and diagnostics execution paths.
- WinUI diagnostic manifest schema expanded with:
  - `dotnet_version`
  - `sdk_resolution_context`
- Failure classifier enhanced to account for XAML compiler input/output artifact state (`input.json` exists + `output.json` missing path pattern).

## Validation Executed

Commands:

```powershell
dotnet build host\HostCore\HostCore.csproj -c Release
dotnet build host\WpfHost\WpfHost.csproj -c Release --no-restore
dotnet build host\WinUiHost\WinUiHost.csproj -c Release -p:Platform=x64 --no-restore
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi -WinUiRestoreRetryCount 1 -NuGetProbeTimeoutSeconds 6
```

Observed:

- `HostCore`: PASS
- `WpfHost`: PASS
- `WinUiHost`: FAIL (unchanged signature)
- `publish_hosts.ps1`:
  - WPF publish + launch smoke: PASS
  - WinUI publish: FAIL (`MSB3073`, `XamlCompiler.exe`)
  - Diagnostics manifest generated with new fields (`dotnet_version=8.0.418`, sdk context present)

## WinUI Blocker Status

Still open:

- `failure_class=TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
- `managed diagnostics`: `WMC9999` persists
- A controlled package-version alignment trial on `Microsoft.WindowsAppSDK` did not resolve the issue and was reverted.

Primary evidence:

- `build/reports/host_publish_latest.txt`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_managed_diag.log`
