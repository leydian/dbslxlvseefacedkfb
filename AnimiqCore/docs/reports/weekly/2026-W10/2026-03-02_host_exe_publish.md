# Host EXE Publish Report (2026-03-02)

## Scope

This update adds a reproducible publish path that outputs runnable GUI executables for both host tracks:

- WPF host
- WinUI host

Distribution contract:

- minimum required payload per host: `*.exe + nativecore.dll`

## Implemented Changes

### 1) Project-level publish defaults

`host/WpfHost/WpfHost.csproj`

- `RuntimeIdentifier=win-x64`
- `SelfContained=true`
- `PublishSingleFile=true`
- `PublishTrimmed=false`

`host/WinUiHost/WinUiHost.csproj`

- `RuntimeIdentifier=win-x64`
- `SelfContained=true`
- `PublishSingleFile=true`
- `PublishTrimmed=false`
- `WindowsAppSDKSelfContained=true`

Purpose:

- keep local publish commands short and deterministic
- avoid runtime dependency on a preinstalled .NET runtime
- reduce packaging variance across environments

### 2) End-to-end publish automation script

Added `tools/publish_hosts.ps1`.

Script behavior:

1. Ensure required tools are available (`cmake`, `dotnet`).
2. Optionally build native release artifacts (`cmake --build build --config Release`).
3. Publish WPF host with self-contained single-file options.
4. Publish WinUI host with self-contained single-file options (+ Windows App SDK self-contained).
5. Create/refresh distribution directories:
   - `dist/wpf`
   - `dist/winui`
6. Copy publish output and force-copy `build/Release/nativecore.dll` into both distributions.
7. Emit a machine-readable run report:
   - `build/reports/host_publish_latest.txt`

Supported parameters:

- `-Configuration` (default: `Release`)
- `-RuntimeIdentifier` (default: `win-x64`)
- `-SkipNativeBuild` (use existing native binaries)

### 3) Output contract

Expected artifacts after successful run:

- `dist/wpf/WpfHost.exe`
- `dist/wpf/nativecore.dll`
- `dist/winui/WinUiHost.exe`
- `dist/winui/nativecore.dll`
- `build/reports/host_publish_latest.txt`

## Verification Summary

Primary command:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1
```

Alternative (skip native rebuild):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild
```

Runtime checks performed by script:

- native binary presence validation:
  - `build/Release/nativecore.dll`
- publish output directory validation for both hosts
- executable path discovery and report serialization

CI smoke validation:

- Workflow: `.github/workflows/host-publish.yml`
- Runner: `windows-latest`
- Sequence:
  1. `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
  2. `cmake --build build --config Release`
  3. `tools/publish_hosts.ps1 -SkipNativeBuild`
  4. Required artifact assertions:
     - `dist/wpf/WpfHost.exe`
     - `dist/wpf/nativecore.dll`
     - `dist/winui/WinUiHost.exe`
     - `dist/winui/nativecore.dll`
     - `build/reports/host_publish_latest.txt`
  5. Upload artifact bundle (`host-publish-outputs`)

## Known Limitations

- Host publish is currently Windows-focused (`win-x64`) in defaults.
- WinUI publish still depends on a valid Windows App SDK-capable environment.
- Script expects CMake build directory layout at `NativeAnimiq/build`.

## Next Steps

- Add optional checksum/signature output for `dist/*` artifacts.
- Add optional packaging mode (`zip`) per host output folder.
