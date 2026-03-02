# Changelog

All notable implementation changes in this workspace are documented here.

## 2026-03-02 - VSFAvatar phase 2 kickoff (UnityFS metadata deep parse)

### Summary

Started the second implementation track for `.vsfavatar` compatibility by moving from header-level probing to metadata-level parsing.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Extended `UnityFsProbe` with metadata diagnostics:
    - `metadata_parsed`
    - `block_count`
    - `node_count`
    - `first_node_path`
    - `metadata_error`

- `src/vsf/unityfs_reader.cpp`
  - Implemented metadata offset resolution logic for UnityFS bundle variants.
  - Added metadata decompression path for `LZ4` and `LZ4HC`.
  - Added UnityFS metadata table parsing:
    - block info table
    - node table
    - first node path extraction
  - Added structured metadata parse failure reporting.

- `src/avatar/vsfavatar_loader.cpp`
  - Added loader warning output for parsed metadata summary.
  - Added loader warning output for metadata parse failure reasons.
  - Updated `missing_features` behavior to avoid reporting metadata decompression as missing when parse succeeds.

### Verified

- Release rebuild succeeded after parser changes.
- `avatar_tool.exe` executed against multiple files in `D:\dbslxlvseefacedkfb\sample`.
- Confirmed metadata diagnostics in runtime output:
  - parsed metadata status true on tested samples
  - `blocks=1`, `nodes=2`
  - first node path reported as `CAB-...`

### Remaining gap after this update

- SerializedFile object table decode is not implemented.
- Mesh/Material/Texture extraction is not implemented.

## 2026-03-02 - VSFAvatar phase 2 continuation (object-table pipeline wiring)

### Summary

Wired the full VSFAvatar object-table extraction path after metadata parse, including serialized file parser scaffolding and sample report automation.

### Added

- `include/vsfclone/vsf/serialized_file_reader.h`
- `src/vsf/serialized_file_reader.cpp`
  - Added `SerializedFileReader::ParseObjectSummary`:
    - parses SerializedFile metadata/object table in a best-effort mode
    - extracts object counts and major Unity class distributions

- `tools/vsfavatar_sample_report.ps1`
  - Runs `avatar_tool.exe` against sample `.vsfavatar` files
  - writes report to `build/reports/vsfavatar_probe.txt`

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Extended `UnityFsProbe` with object-table fields:
    - `object_table_parsed`, `object_count`
    - `mesh_object_count`, `material_object_count`, `texture_object_count`
    - `game_object_count`, `skinned_mesh_renderer_count`
    - `major_types_found`

- `src/vsf/unityfs_reader.cpp`
  - Added metadata table structs (`BlockInfo`, `NodeInfo`) and parsing
  - Added data-stream reconstruction attempt from parsed block table
  - Added node-level SerializedFile summary extraction attempts
  - Added detailed reconstruction diagnostics by offset candidate

- `src/avatar/vsfavatar_loader.cpp`
  - Updated warning output to include serialized diagnostics
  - Updated mesh/material placeholder population from discovered object counts
  - Refined missing-feature messages for staged progress

- `CMakeLists.txt`
  - Added `src/vsf/serialized_file_reader.cpp` to `vsfclone_core`

### Verified

- Release build succeeded after integration.
- Sample probe script executed successfully on sample `.vsfavatar` files.
- Metadata parse remains successful; object-table path is now executed and emits diagnostics.

### Current blocker

- Current sample set fails during bundle data block decompression in reconstruction stage (`LZ4 decode failed`).
- As a result, object table summary extraction does not complete on those samples yet.

## 2026-03-02 - NativeCore foundation + avatar pipeline extension

### Summary

Implemented the first end-to-end native runtime foundation for the VSeeFace-style standalone app effort.  
This update moves the project from a scaffold CLI into a reusable runtime DLL model with explicit API contracts and richer avatar compatibility diagnostics.

### Added

- `include/vsfclone/nativecore/api.h`
  - New exported C ABI contract for host applications.
  - Stable primitive structs for init/load/render/tracking/broadcast flows.
  - Error/result codes designed for cross-language interop.

- `src/nativecore/native_core.cpp`
  - Runtime state manager with guarded global state (`std::mutex`).
  - Avatar handle lifecycle (`load -> query -> unload`).
  - Last-error propagation via `nc_get_last_error`.
  - Tracking and render entrypoints stabilized as callable placeholders.
  - Spout/OSC integration points wired to existing stub backends.

- `src/avatar/vxavatar_loader.h`
- `src/avatar/vxavatar_loader.cpp`
  - New `.vxavatar` loader route.
  - ZIP signature probing (`PK` magic) for initial format validation.
  - Diagnostic reporting for missing parser stages.

- `tools/avatar_tool.cpp`
  - New runtime API sanity tool.
  - Exercises `nativecore.dll` instead of direct facade calls.
  - Prints normalized format/compatibility/diagnostic information.

### Changed

- `include/vsfclone/avatar/avatar_package.h`
  - Added `AvatarSourceType::VxAvatar`.
  - Added `AvatarCompatLevel` enum.
  - Added `compat_level` field.
  - Added `missing_features` list.

- `src/avatar/avatar_loader_facade.cpp`
  - Registered `VxAvatarLoader` in extension dispatch chain.

- `src/avatar/vrm_loader.cpp`
  - Added compatibility/missing-feature diagnostics for scaffold state.

- `src/avatar/vsfavatar_loader.cpp`
  - Added compatibility classification.
  - Added explicit pending-feature diagnostics for UnityFS deep parse path.

- `src/main.cpp`
  - Added `VXAvatar` source type display support.
  - Replaced non-ASCII usage sample path with ASCII-safe sample path.

- `CMakeLists.txt`
  - Converted `vsfclone_core` to static internal library.
  - Added shared library target: `nativecore`.
  - Added executable target: `avatar_tool`.
  - Wired include paths and export macro definition for DLL build.

- `build.ps1`
  - Updated build output summary to include `nativecore.dll` and `avatar_tool.exe`.

- `README.md`
  - Updated current capabilities.
  - Documented API and runtime scope.
  - Added implementation summary and verification notes.

### Verified

- CMake configure + MSVC Release build succeeded.
- Built artifacts produced:
  - `build/Release/nativecore.dll`
  - `build/Release/vsfclone_cli.exe`
  - `build/Release/avatar_tool.exe`
- `avatar_tool.exe` tested with a real `.vsfavatar` file:
  - Load success
  - Detected format: `VSFAvatar`
  - Compatibility: `partial`
  - Missing-feature diagnostics returned as expected

### Known gaps after this update

- DX11 renderer is not implemented yet (render call is placeholder).
- VRM decode + MToon binding are not implemented.
- `.vxavatar` manifest/material override parser is not implemented.
- `.vsfavatar` deep object extraction is not implemented.
- MediaPipe webcam tracking integration is not implemented.
- WinUI/WPF host app project is not created yet.
