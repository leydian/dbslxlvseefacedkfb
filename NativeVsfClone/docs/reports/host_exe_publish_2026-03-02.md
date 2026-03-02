# Host EXE Publish Report (2026-03-02)

## Goal

Produce runnable GUI host artifacts as self-contained executables for both host tracks:

- WPF host
- WinUI host

Distribution policy is fixed to:

- `exe + nativecore.dll` (2 files minimum in output folder)

## Build/Publish Pipeline

New script:

- `tools/publish_hosts.ps1`

Pipeline stages:

1. Build native release artifacts:
   - `cmake --build build --config Release`
2. Publish WPF host:
   - `dotnet publish host/WpfHost/WpfHost.csproj ...`
3. Publish WinUI host:
   - `dotnet publish host/WinUiHost/WinUiHost.csproj ...`
4. Copy `build/Release/nativecore.dll` into each host distribution folder.
5. Consolidate output into:
   - `dist/wpf`
   - `dist/winui`
6. Write publish report:
   - `build/reports/host_publish_latest.txt`

## Project Publish Defaults

### WpfHost

`host/WpfHost/WpfHost.csproj`

- `RuntimeIdentifier=win-x64`
- `SelfContained=true`
- `PublishSingleFile=true`
- `PublishTrimmed=false`

### WinUiHost

`host/WinUiHost/WinUiHost.csproj`

- `RuntimeIdentifier=win-x64`
- `SelfContained=true`
- `PublishSingleFile=true`
- `PublishTrimmed=false`
- `WindowsAppSDKSelfContained=true`

## Run Command

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1
```

Optional:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild
```

## Expected Output

- `dist/wpf/WpfHost.exe`
- `dist/wpf/nativecore.dll`
- `dist/winui/WinUiHost.exe`
- `dist/winui/nativecore.dll`
- `build/reports/host_publish_latest.txt`

## Validation Notes

If publish fails:

- check .NET SDK installation (`dotnet --version`)
- check Windows App SDK workload for WinUI
- verify `build/Release/nativecore.dll` exists
