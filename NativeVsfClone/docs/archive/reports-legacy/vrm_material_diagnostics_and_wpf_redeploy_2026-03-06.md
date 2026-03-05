# VRM Material Diagnostics + WPF Redeploy (2026-03-06)

## Summary

This update implements the VRM render-path diagnostics plan focused on material/alpha visibility parity and ships a refreshed WPF distribution.

Primary outcomes:

- Added structured material diagnostics to runtime payloads.
- Normalized VRM alpha mode parsing (`OPAQUE`/`MASK`/`BLEND`) and cutoff clamping.
- Exposed material diagnostics through native ABI and host diagnostics UI.
- Extended CLI/gate tooling to assert material diagnostics presence.
- Re-published WPF host (`dist/wpf`) and verified launch smoke pass.

## Scope of Code Changes

### 0) Workspace-integrated Spout2 scaffold (included in this commit)

The current workspace also contains a staged Spout2 interop scaffold track, which is included together with this update:

- `CMakeLists.txt`
- `src/stream/spout_sender.cpp`
- `include/vsfclone/stream/spout_sender.h`
- `tools/publish_hosts.ps1`
- `third_party/Spout2/include/...` (DX/DX12/DX9/GL/Library headers)
- `third_party/Spout2/lib/SpoutDX.lib`
- `third_party/Spout2/lib/SpoutLibrary.lib`

Note: this report’s deep-dive sections focus on the VRM material diagnostics/redeploy track, while the Spout2 scaffold is recorded here for full commit coverage.

### 1) Runtime payload model and VRM loader

- `include/vsfclone/avatar/avatar_package.h`
  - added `MaterialDiagnosticsEntry`
  - added `AvatarPackage.material_diagnostics`
- `src/avatar/vrm_loader.cpp`
  - added `NormalizeAlphaMode(...)`
  - normalized parsed `alphaMode` into canonical uppercase values
  - clamped `alphaCutoff` to `[0, 1]`
  - populated `MaterialDiagnosticsEntry` per material (alpha, double-sided, texture presence, typed-param counts, MToon-binding flag)
  - emitted default material diagnostics entry for fallback material path

### 2) Native ABI + runtime info exposure

- `include/vsfclone/nativecore/api.h`
  - extended `NcAvatarInfo` with:
    - `material_diag_count`
    - `last_material_diag`
- `src/nativecore/native_core.cpp`
  - added material diagnostics summary builder
  - wrote diagnostics count and summary into `NcAvatarInfo` in `FillAvatarInfo(...)`

### 3) Host interop and diagnostics UI

- `host/HostCore/NativeCoreInterop.cs`
  - mirrored new `NcAvatarInfo` fields:
    - `MaterialDiagCount`
    - `LastMaterialDiag`
- `host/WpfHost/MainWindow.xaml.cs`
  - avatar diagnostics view now shows `MaterialDiagCount` and `LastMaterialDiag`
- `host/WinUiHost/MainWindow.xaml.cs`
  - avatar diagnostics view now shows `MaterialDiagCount` and `LastMaterialDiag`

### 4) Tooling + quality gate

- `tools/avatar_tool.cpp`
  - prints `MaterialDiagnostics` count
  - prints `LastMaterialDiag` summary
- `tools/vrm_quality_gate.ps1`
  - parses `MaterialDiagnostics`
  - GateC strengthened: requires material diagnostics > 0 in addition to material/texture payload checks
  - summary now includes per-sample `materialDiag` counts

## Validation Results

### Build checks

- `cmake --build .\build --config Release --target nativecore avatar_tool` : PASS
- `dotnet build host\HostCore\HostCore.csproj -c Release` : PASS
- `dotnet build host\WpfHost\WpfHost.csproj -c Release` : PASS

Additional note:

- `dotnet build host\WinUiHost\WinUiHost.csproj -c Release` hit existing environment/toolchain-level WinUI XamlCompiler failure (`MSB3073`), not caused by this change set.

### VRM quality gate

- `powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -SampleDir ..\sample -AvatarToolPath .\build\Release\avatar_tool.exe -Profile fixed5` : PASS
- All fixed5 samples reported `materialDiag > 0`.

## Redeploy Results

Executed:

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -Configuration Release -RuntimeIdentifier win-x64 -NoRestore`

Outcome:

- WPF publish completed (`HostPublishMode: WPF_ONLY`)
- launch smoke test PASS
- refreshed runtime distribution:
  - `dist/wpf/WpfHost.exe`
  - `dist/wpf/nativecore.dll`
- publish report:
  - `build/reports/host_publish_latest.txt`
  - `build/reports/wpf_launch_smoke_latest.txt`

## Operational Impact

- Operators can now distinguish `runtime-ready` parser success from per-material render-contract quality through explicit material diagnostics.
- Regression gate now fails early when material payload extraction appears non-empty but diagnostics are missing.
- WPF runtime package is refreshed and smoke-verified for immediate validation on the target host path.
