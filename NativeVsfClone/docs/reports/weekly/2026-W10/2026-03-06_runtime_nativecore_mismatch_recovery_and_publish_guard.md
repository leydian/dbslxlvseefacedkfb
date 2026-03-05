# Runtime NativeCore Mismatch Recovery + Publish Guard Hardening (2026-03-06)

## Summary

This change set resolves the recurring symptom where VRM avatars loaded as `runtime-ready` still rendered with severe material corruption while host diagnostics showed:

- `MaterialDiagCount: 0`
- `LastMaterialDiag: (empty)`

Root cause was confirmed as **runtime binary mismatch**, not parser/loader logic failure:

- CLI path (`build/Release/avatar_tool.exe`) on the same VRM reported `MaterialDiagnostics: 14`.
- Host runtime path was loading a stale `nativecore.dll` in certain runs.

This update adds two protections:

1. immediate runtime visibility of the **actually loaded** `nativecore.dll` path/timestamp in host diagnostics
2. hard fail/verification guards in publish flow so stale native DLLs cannot silently ship

## Root-Cause Evidence

Validated on sample:

- `sample/개인작10-2.vrm`

Observed:

- host UI previously reported `MaterialDiagCount: 0`
- `avatar_tool` reported:
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`
  - `MaterialDiagnostics: 14`
  - non-empty `LastMaterialDiag`

Interpretation:

- VRM material diagnostics generation logic was functioning.
- runtime process was not consistently using the same `nativecore.dll` binary as CLI validation path.

## Implementation Details

### 1) Host runtime diagnostics: loaded native module identity

Updated:

- `host/HostCore/NativeCoreInterop.cs`
- `host/HostCore/DiagnosticsModel.cs`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- Added native module inspection helpers:
  - `GetLoadedNativeCorePath()`
  - `GetLoadedNativeCoreTimestampUtc()`
- Added runtime diagnostics fields:
  - `NativeCoreModulePath`
  - `NativeCoreModuleTimestampUtc`
- Runtime diagnostics tabs (WPF/WinUI) now print those fields directly.

Operational impact:

- Operator can now verify in-app exactly which `nativecore.dll` is loaded.
- Binary mismatch can be detected immediately without external tooling.

### 2) Publish hardening: fail-fast cmake + DLL integrity check

Updated:

- `tools/publish_hosts.ps1`

Changes:

- Added `Invoke-CMakeCommand` wrapper:
  - executes cmake
  - checks `$LASTEXITCODE`
  - throws on non-zero exit
- Replaced raw cmake calls in native build/fallback flow with guarded wrapper.
- Added `Assert-NativeCoreCopyIntegrity`:
  - checks source/destination DLL existence
  - computes SHA256 for `build` vs `dist` copy
  - fails publish if hash mismatch
  - logs source/destination path, timestamps, hashes
- Applied integrity assertion to:
  - WPF dist copy
  - WinUI dist copy

Operational impact:

- publish no longer silently passes when cmake command output contains build errors but script flow continues.
- stale or mismatched `nativecore.dll` copy now fails publish deterministically.

## Verification

Executed:

1. `dotnet build host/HostCore/HostCore.csproj -c Release` (pass)
2. `dotnet build host/WpfHost/WpfHost.csproj -c Release` (pass)
3. `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -Configuration Release -RuntimeIdentifier win-x64 -NoRestore -SkipNativeBuild` (pass)
4. SHA256 equality check:
   - `build/Release/nativecore.dll`
   - `dist/wpf/nativecore.dll`
   - result: `MATCH`
5. `build/Release/avatar_tool.exe sample/개인작10-2.vrm`:
   - `MaterialDiagnostics: 14`
   - non-empty `LastMaterialDiag`

Publish report now includes integrity evidence:

- `build/reports/host_publish_latest.txt`
  - source/destination nativecore paths
  - source/destination timestamps
  - source/destination hashes

## Notes

- This update is focused on runtime DLL consistency and publish guardrails.
- It intentionally does not alter VRM material extraction semantics in this pass.
