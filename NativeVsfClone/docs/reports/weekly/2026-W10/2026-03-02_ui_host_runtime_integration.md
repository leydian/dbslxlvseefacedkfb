# UI Host Runtime Integration Report (2026-03-02)

## Summary

This update implemented the UI integration baseline and runtime output path:

- Added shared host-layer interop/services (`host/HostCore`)
- Added host shells for both WPF and WinUI (`host/WpfHost`, `host/WinUiHost`)
- Extended `nativecore.dll` with window-bound rendering APIs
- Replaced Spout/OSC stubs with runtime output implementations

The goal was to enable an end-to-end path:

`initialize -> load avatar -> create render resources -> render loop -> spout/osc output -> unload/shutdown`

## Native API Changes

File: `include/vsfclone/nativecore/api.h`

- Added types:
  - `NcWindowRenderTarget`
  - `NcRuntimeStats`
- Added APIs:
  - `nc_create_window_render_target`
  - `nc_resize_window_render_target`
  - `nc_destroy_window_render_target`
  - `nc_render_frame_to_window`
  - `nc_get_runtime_stats`

Existing APIs were kept intact for backward compatibility.

## Native Runtime Implementation

File: `src/nativecore/native_core.cpp`

- Added swapchain-per-window lifecycle management (D3D11)
- Added shared render path for:
  - external render context (`nc_render_frame`)
  - window-bound render (`nc_render_frame_to_window`)
- Added runtime frame timing stat (`last_frame_ms`)
- Added render-time output hooks:
  - RTV capture -> Spout sender
  - tracking values -> OSC publish (`/VsfClone/Tracking/*`)

## Output Backend Changes

### Spout path

Files:

- `include/vsfclone/stream/spout_sender.h`
- `src/stream/spout_sender.cpp`

Implementation details:

- Replaced no-op stub with shared-memory BGRA frame sender
- Mapped-file channel naming: `Local\VsfCloneSpout_<channel>`
- Header + pixel payload write on each frame submission

### OSC path

Files:

- `include/vsfclone/osc/osc_endpoint.h`
- `src/osc/osc_endpoint.cpp`

Implementation details:

- Replaced no-op stub with Winsock UDP endpoint
- Internal OSC packet writer for float payloads
- Configurable destination via `publish_address` (`host:port`)

## Build System Changes

File: `CMakeLists.txt`

- Replaced stub source entries with runtime source entries
- Added Windows link libs for nativecore:
  - `d3d11`
  - `dxgi`
  - `ws2_32`

## Host Layer Additions

### Shared host core

Folder: `host/HostCore`

- `NativeCoreInterop.cs`: P/Invoke mapping for native API
- `AvatarSessionService.cs`: init/load/unload/shutdown workflow
- `RenderLoopService.cs`: window target attach/resize/tick
- `OutputService.cs`: spout/osc start-stop control
- `DiagnosticsModel.cs`: runtime stat + last-error snapshot

### WPF host

Folder: `host/WpfHost`

- MVP UI for load/unload, render loop, spout/osc toggles
- Diagnostics panel showing parser/runtime/output state

### WinUI host

Folder: `host/WinUiHost`

- Parallel MVP shell with same core features
- Shared service path via `host/HostCore`

### Host solution

- `host/HostApps.sln`

## Verification

### Native build

Command:

```powershell
cmake --build build --config Release
```

Result: succeeded, including `nativecore.dll`.

### Native smoke test

Command:

```powershell
.\build\Release\avatar_tool.exe "..\sample\Kikyo_FT Variant.vrm"
```

Result: load succeeded, `ParserStage=runtime-ready`, payload counts valid.

## Known Constraints

- `.NET SDK` was not available in this environment (`dotnet` missing), so WPF/WinUI build/run validation was not executed here.
- Spout path is currently implemented as internal shared-memory transport, not full official Spout2 SDK interop.
- Render path still clears RTV (minimal render baseline); full mesh/material draw path is a subsequent step.
