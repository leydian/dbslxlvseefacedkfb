# VsfCloneNative

Native C++ scaffold for a standalone VTuber-style runtime with:

- `.vrm` input path handling (scaffold)
- `.vxavatar` input path handling (scaffold)
- `.vsfavatar` probing via UnityFS header parser (implemented)
- `nativecore.dll` C ABI for host/UI integration
- streaming and OSC interfaces (`Spout2`, `OSC`) as stubs for wiring

## What is implemented now

- `AvatarLoaderFacade` dispatches by extension (`.vrm`, `.vxavatar`, `.vsfavatar`).
- `VsfAvatarLoader` reads UnityFS metadata and reports:
  - signature/version/player version/engine version
  - compression mode flags
  - basic token probes (`CAB-`, `VRM`) in early data window
- `nativecore.dll` exports a stable API:
  - init/shutdown
  - avatar load/query/unload
  - tracking frame submission
  - render tick (placeholder)
  - Spout/OSC start-stop stubs
  - last error retrieval
- `vsfclone_cli` and `avatar_tool` print structured load diagnostics.

## What is not implemented yet

- Full UnityFS block decompression and object table reconstruction
- SerializedFile class parsing (`GameObject`, `Mesh`, `Material`, etc.)
- Real VRM import pipeline (glTF/VRM decode and MToon binding)
- `.vxavatar` manifest/material override parse/apply
- DirectX11 renderer and WinUI/WPF host integration
- Real Spout2 and OSC runtime bindings

## Build (Windows)

Prerequisites:

- Visual Studio 2022 Build Tools (MSVC)
- CMake 3.20+

Commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run:

```powershell
.\build\Release\vsfclone_cli.exe "D:\dbslxlvseefacedkfb\개인작11-3.vsfavatar"
.\build\Release\avatar_tool.exe "D:\path\to\avatar.vxavatar"
```

## Repository layout

- `include/vsfclone/...`: public interfaces and types
- `include/vsfclone/nativecore/api.h`: C ABI contract
- `src/avatar`: avatar loaders and facade
- `src/vsf`: UnityFS probe/parser code
- `src/nativecore`: C ABI implementation
- `src/stream`, `src/osc`: integration stubs
- `src/main.cpp`: facade CLI entrypoint
- `tools/avatar_tool.cpp`: `nativecore.dll` sanity CLI

## Recent implementation summary (2026-03-02)

This repository was upgraded from a loader-only scaffold to a first executable runtime slice centered on `nativecore.dll`.

### Key functional additions

- Added a C ABI surface for host integration in `include/vsfclone/nativecore/api.h`.
- Implemented `nativecore.dll` runtime state and exported API handlers in `src/nativecore/native_core.cpp`.
- Added `.vxavatar` extension support and registered it in the loader facade.
- Expanded avatar diagnostics with compatibility level and missing feature reporting.
- Added `avatar_tool.exe` for API-level smoke testing independent from the facade CLI.
- Updated build scripts and CMake targets to produce `nativecore.dll`, `avatar_tool.exe`, and `vsfclone_cli.exe`.

### API surface now available

- Lifecycle: `nc_initialize`, `nc_shutdown`
- Avatar: `nc_load_avatar`, `nc_get_avatar_info`, `nc_unload_avatar`
- Runtime hooks: `nc_set_tracking_frame`, `nc_render_frame`
- Broadcast stubs: `nc_start_spout`, `nc_stop_spout`, `nc_start_osc`, `nc_stop_osc`
- Diagnostics: `nc_get_last_error`

### Current behavior status

- `.vrm`: accepted by extension route, returns scaffold diagnostics.
- `.vxavatar`: checks ZIP signature and returns compatibility diagnostics.
- `.vsfavatar`: UnityFS header/token probing works; deep object extraction is pending.
- Render and tracking paths are API-stable placeholders, not full runtime implementations yet.

### Verification done

- Full `Release` build completed successfully with MSVC/CMake.
- `avatar_tool.exe` was executed against a real `.vsfavatar` file and returned expected `partial` compatibility diagnostics.

## Recent implementation summary (2026-03-02, phase 2 start)

Started the `.vsfavatar` compatibility deepening track by extending UnityFS parsing beyond simple header probing.

### What changed

- Extended `UnityFsProbe` with metadata diagnostics:
  - `metadata_parsed`
  - `block_count`
  - `node_count`
  - `first_node_path`
  - `metadata_error`
- Added metadata block parsing flow in `UnityFsReader`:
  - metadata offset resolution (`header-end` and `end-of-file` flag path)
  - LZ4/LZ4HC raw decompression for metadata
  - block info table parsing
  - node table parsing
  - first node path extraction
- Updated `VsfAvatarLoader` warning logic:
  - reports parsed metadata summary (`blocks`, `nodes`, `first node`)
  - reports metadata parse failure reason when unavailable
  - removes metadata decompression from `missing_features` when metadata is parsed successfully

### Validation with sample dataset

- Rebuilt `Release` successfully.
- Ran `avatar_tool.exe` on multiple files in `D:\dbslxlvseefacedkfb\sample`.
- Confirmed metadata parse diagnostics for tested samples:
  - `blocks=1`
  - `nodes=2`
  - first node path observed as `CAB-...`

### Current remaining gap in this track

- SerializedFile object table decode is still pending.
- Mesh/material/texture extraction from parsed Unity asset objects is still pending.
