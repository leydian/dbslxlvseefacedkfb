# WPF Runtime Path Guard + Dist Alignment (2026-03-06)

## Summary

This pass resolves recurring "model still broken" reports where code fixes were present in source/build outputs but not reflected in the operator runtime.

Primary issue:

- runtime execution path and deployed binaries were not guaranteed to match
- users often launch `dist/wpf/WpfHost.exe`, while newer binaries existed under `host/.../bin/Release`

Result:

- visual behavior appeared unchanged despite code updates
- diagnosis was noisy because runtime path provenance was not explicitly surfaced

## Root Cause Confirmation

Observed during triage:

- `avatar_tool` on current workspace build loaded `sample/개인작10-2.vrm` successfully (`opaque=10, mask=3, blend=1`)
- `dist/wpf` binaries were older than current `bin/Release` outputs before republish

This confirmed a runtime artifact mismatch, not just a loader logic issue.

## Implementation Details

### 1) Runtime path diagnostics contract

Updated:

- `host/HostCore/DiagnosticsModel.cs`

Added fields:

- `ExpectedNativeCoreModulePath`
- `RuntimePathMatch`
- `RuntimePathWarningCode`

Behavior:

- compute expected runtime DLL as `AppContext.BaseDirectory/nativecore.dll`
- compare with actually loaded nativecore module path
- emit warning codes:
  - `HOST_RUNTIME_PATH_UNKNOWN`
  - `HOST_RUNTIME_MISMATCH_DIST_EXPECTED`

### 2) Load-time safety gate in WPF host

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Behavior:

- before `LoadAvatar`, check `LastSnapshot.Runtime.RuntimePathMatch`
- if mismatch:
  - block load
  - reveal diagnostics
  - show loaded/expected paths and warning code in failure message

### 3) Runtime diagnostics visibility

Updated runtime text output to always include:

- `ExpectedNativeCoreModulePath`
- `RuntimePathMatch`
- `RuntimePathWarningCode`

This allows immediate operator-side confirmation that runtime points to expected deploy artifacts.

## Deployment / Verification

### Build

- `dotnet build host/WpfHost/WpfHost.csproj -c Release` (pass)

Note:

- initial restore/build failed under sandbox with `NU1301`
- rerun with elevated network permission succeeded

### Dist publish alignment

- ran `tools/publish_hosts.ps1` (pass)
- confirmed `dist/wpf/WpfHost.exe` regenerated
- confirmed `dist/wpf/nativecore.dll` copied from latest native build
- verified source/destination `nativecore.dll` SHA256 hashes match in `build/reports/host_publish_latest.txt`

## Operational Outcome

- stale or mismatched runtime-path executions are now detectable and blocked at load time
- diagnostics explicitly guide operator to correct `dist/wpf` runtime artifacts
- repeated false-negative validation loops ("still broken after patch") are reduced

