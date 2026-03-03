# Host Stabilization Round Report (2026-03-03)

## Summary

This round hardened host publish/build reliability under restore/network failure conditions and validated WinUI diagnostic artifact generation on failure paths.

Restore/network access is now available in this environment, but WinUI Release build/publish is still blocked by `XamlCompiler.exe` exit code `1` without line-level diagnostics.

## Changes

### 1) `tools/publish_hosts.ps1` failure handling hardening

- Added `-NoRestore` switch for reproducible offline/cached publish attempts.
- Added strict native `dotnet` exit-code checking via `Invoke-DotNetCommand`.
  - publish now fails immediately on non-zero exit code instead of relying only on output file existence.
- Normalized `WinUiDiagDir` to absolute path using `GetFullPath(...)`.
- If WPF publish fails and `-IncludeWinUi` is set, script now still attempts WinUI publish to capture WinUI diagnostics before final failure.
- WinUI diagnostics collection remains enabled by default (`CollectWinUiDiagnostics=true`) and now records `--no-restore` in diagnostic command when selected.

### 2) WinUI diagnostic output verification

Generated and confirmed:

- `build/reports/winui/winui_build.binlog`
- `build/reports/winui/winui_build_diag.log`
- `build/reports/winui/winui_build_stderr.log`
- `build/reports/winui/winui_diagnostic_manifest.json`
- `build/reports/winui/obj-dump/**`

## Verification Commands and Results

Executed:

```powershell
dotnet restore host/WpfHost/WpfHost.csproj -v minimal
dotnet restore host/WinUiHost/WinUiHost.csproj -v minimal
```

- Result: PASS

Executed:

```powershell
dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore
```

- Result: PASS

Executed:

```powershell
dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64 --no-restore
```

- Result: FAIL (`MSB3073`, `XamlCompiler.exe ... output.json`, exit code `1`)

Executed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi
```

- Result: FAIL (WinUI publish step)
- Behavior check: PASS
  - WPF publish success
  - WinUI publish still attempted
  - WinUI diagnostics collected and manifest/log/binlog emitted

Environment facts:

- `dotnet --list-sdks`
  - `9.0.311`

Attempted remediations in this run:

- Cleared WinUI `obj/bin` and re-ran clean build
  - result: no change, `XamlCompiler.exe` still exits with code `1`
- Ran `XamlCompiler.exe` directly with generated `input.json/output.json`
  - result: reproducible non-zero exit without actionable line diagnostics
- Inspected generated `output.json` / diagnostic logs
  - result: no explicit `ErrorCode/File/Line` entries were emitted

## Current Blocker

- WinUI XAML compile path fails at `XamlCompiler.exe` with exit code `1` and no actionable line-level error output from `output.json`/stderr.

## Next Steps

1. Re-run WinUI build with full tooling alignment (SDK/VS/Windows App SDK) and capture fresh `build/reports/winui/*` artifacts.
2. Run host publish end-to-end:
   - `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi`
3. Execute manual smoke on both hosts (once WinUI build/publish succeeds):
   - initialize session
   - load avatar
   - toggle Spout/OSC on/off
   - unload avatar
   - shutdown
