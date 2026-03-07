# AnimiqCore

Native runtime workspace for loading avatar formats (`.vrm`, `.miq`, `.vsfavatar`) and running host apps (`WPF`, `WinUI`) on top of `nativecore.dll`.

## Quick Start (10 minutes)

Prerequisites:

- Windows 10/11
- Visual Studio 2022 Build Tools (MSVC)
- CMake 3.20+
- .NET 8 SDK

Build native binaries:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DANIMIQ_ENABLE_SPOUT2=ON
cmake --build build --config Release
```

Spout2 SDK note:

- if `third_party/Spout2/include` exists, native build enables `ANIMIQ_SPOUT2_ENABLED`
- if not found, build falls back to legacy shared-memory sender automatically

Run quick probes:

```powershell
.\build\Release\animiq_cli.exe "D:\path\to\avatar.vsfavatar"
.\build\Release\avatar_tool.exe "D:\path\to\avatar.vxavatar"
.\build\Release\vrm_to_miq.exe "D:\path\to\avatar.vrm" "D:\path\to\avatar.miq"
```

Publish host app (WPF default):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1
```

Optional WinUI diagnostics track:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi
```

## Current Validation Snapshot (2026-03-07)

- `tools/vsfavatar_quality_gate.ps1 -UseFixedSet`: PASS
- `tools/vrm_quality_gate.ps1 -Profile fixed5`: PASS
- `tools/publish_hosts.ps1`: WPF PASS (`ReleaseCandidateWpfOnly: PASS`), WinUI `WMC9999` triaged.
- `Onboarding KPI Gate`: PASS (5 sessions verified)

For webcam setup details, see: [docs/public/webcam-mediapipe-setup.md](./docs/public/webcam-mediapipe-setup.md)

## Core Runtime Surface

- Avatar load/query/unload (`.vrm`, `.miq`, `.vsfavatar`)
- Tracking frame submit + render tick
- Spout/OSC output controls
- Runtime diagnostics and last-error contracts
- Shared host controller layer in `host/HostCore`

For detailed API and implementation history, see:

- [CHANGELOG.md](./CHANGELOG.md)
- [Documentation Index](./docs/INDEX.md)

## Parser Mode Controls (`.vsfavatar`)

```powershell
$env:VSF_PARSER_MODE = "sidecar"        # default
$env:VSF_PARSER_MODE = "inhouse"        # bypass sidecar
$env:VSF_PARSER_MODE = "sidecar-strict" # no fallback
$env:VSF_SIDECAR_PATH = "D:\custom\vsfavatar_sidecar.exe"
$env:VSF_SIDECAR_TIMEOUT_MS = "15000"
```

## Quality Gates

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5
powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick
powershell -ExecutionPolicy Bypass -File .\tools\spout2_interop_gate.ps1 -RequireSpout2Configured
powershell -ExecutionPolicy Bypass -File .\tools\wpf_user_regression_pack.ps1 -SkipNativeBuild -NoRestore
powershell -ExecutionPolicy Bypass -File .\tools\release_dual_lane_gate.ps1 -SkipNativeBuild -NoRestore
powershell -ExecutionPolicy Bypass -File .\tools\release_blocker_burndown.ps1
```

## Repository Map

- `include/animiq/`: public interfaces
- `src/`: runtime and loaders
- `host/`: WPF/WinUI apps + shared HostCore
- `unity/Packages/com.animiq.miq/`: Unity package scaffold
- `tools/`: gates, publish, diagnostics automation
- `docs/`: specs, reports, and documentation rules

## Documentation Policy

- Keep onboarding instructions in this file.
- Keep detailed implementation evidence in `docs/reports/*.md`.
- Keep chronological change summaries in `CHANGELOG.md`.
- Run docs checks before merging docs updates:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\docs_quality_gate.ps1
```
