# Runtime Stale Guard (WPF/WinUI) + Common-Cause Triage (2026-03-06)

## Summary

This pass targeted a clustered runtime symptom reported as:

- arm movement not applying
- realtime shadow not visible
- facial expression not applying

The objective was to harden host-side runtime provenance checks so stale runtime artifacts are blocked earlier, and to reduce false triage loops where source fixes exist but operator runtime still executes older binaries.

## Root-Cause Evidence Collected

### 1) Current `dist/wpf` vs latest native build hash (now aligned)

- `dist/wpf/nativecore.dll` SHA256 matched `build/Release/nativecore.dll` in this workspace snapshot.
- This confirms the current `dist/wpf` copy is up to date.

### 2) Multi-path runtime drift exists in workspace

A workspace sweep for `WpfHost.exe` + sibling `nativecore.dll` showed multiple executable paths with differing nativecore hashes:

- some paths matched latest build hash
- multiple other paths were stale
- one publish path had no sibling `nativecore.dll`

This is sufficient evidence that launching a non-dist path can reproduce "code fixed but symptom unchanged" behavior.

### 3) Avatar payload inspection baseline

`avatar_tool` inspection for `개인작10-2.xav2` reported:

- `ExpressionCount: 52`
- `ExpressionBindTotal: 57`
- warning code `XAV2_EXPRESSION_CATALOG_SYNTHESIZED`

This confirms the inspected payload itself is not in an empty-expression state.

## Implemented Changes

### 1) WPF load-time stale runtime block

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Behavior added:

- existing path mismatch gate retained (`RuntimePathMatch == false`)
- new stale gate added:
  - if `RuntimeModuleStaleVsBuildOutput == true`, block `LoadAvatar`
  - surface loaded/build paths, UTC timestamps, and `RuntimeTimestampWarningCode`

Operator outcome:

- stale runtime is explicitly blocked before avatar load
- remediation message points to republish and relaunch flow

### 2) WinUI load-time runtime provenance gate parity

Updated:

- `host/WinUiHost/MainWindow.xaml.cs`

Behavior added:

- block load on `RuntimePathMatch == false` with loaded/expected path detail
- block load on `RuntimeModuleStaleVsBuildOutput == true` with timestamp/warning detail

### 3) WinUI runtime diagnostics visibility expansion

Updated runtime diagnostics text to include:

- `ExpectedNativeCoreModulePath`
- `BuildNativeCoreModulePath`
- `BuildNativeCoreModuleTimestampUtc`
- `RuntimePathMatch`
- `RuntimeModuleStaleVsBuildOutput`
- `RuntimePathWarningCode`
- `RuntimeTimestampWarningCode`

This aligns WinUI operator visibility with WPF runtime provenance signals.

## Verification

Executed:

- `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore` -> PASS
- `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore` -> PASS
- `dotnet build host/WinUiHost/WinUiHost.csproj -c Release --no-restore` -> FAIL (`MSB3073`, local WinUI XamlCompiler environment issue)

Additional checks:

- `dist/wpf/nativecore.dll` and `build/Release/nativecore.dll` hash match confirmed.
- runtime-path sweep identified stale alternate launch locations.

## Impact

- Prevents stale runtime execution from silently masking fixes.
- Makes "three features broken at once" triage decision faster:
  - first decision axis is now explicit runtime provenance (`path/stale`),
  - then policy/data warning-code triage (`arm/expression/shadow`) if runtime provenance is healthy.

## Known Limitations

- WinUI compile verification remains environment-blocked in this machine due to XAML toolchain issue.
- Runtime-path/stale guard blocks load but does not auto-republish artifacts.
