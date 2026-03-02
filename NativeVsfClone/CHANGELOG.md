# Changelog

All notable implementation changes in this workspace are documented here.

## 2026-03-02 - VSFAvatar reconstruction window expansion and block-total diagnostics

### Summary

Added another reconstruction-focused diagnostics pass to improve candidate scoring and expose per-attempt decode evidence.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added additional reconstruction diagnostics:
    - `total_block_compressed_size`
    - `total_block_uncompressed_size`
    - `reconstruction_best_partial_blocks`

- `src/vsf/unityfs_reader.cpp`
  - Updated metadata candidate scoring with block-total plausibility checks against bundle layout.
  - Added reconstruction start-offset expansion:
    - anchor windows (`+/-256`, 16-byte step) around key offsets
    - tail-packed anchor (`bundle_file_size - total_compressed`)
  - Added bounded variant-level decode failure aggregation for block diagnostics.
  - Added LZ4 bounded fallback path when exact-size/frame/size-prefixed decoding all fail.

- `src/avatar/vsfavatar_loader.cpp`
  - Metadata warning now includes:
    - block compressed/uncompressed totals
    - best partial reconstructed block count

### Verified

- `Release` build succeeded.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`, generated `2026-03-02T23:30:48`).
- Current baseline is still `Compat: partial` / `Meshes: 0` for all fixed samples.
- Block-0 diagnostics remain consistent on fixed set:
  - `mode=0`
  - `code=DATA_BLOCK_READ_FAILED`
  - expected sizes: `74890067`, `88135067`, `125513796`, `402596`

## 2026-03-02 - VSFAvatar block-layout candidate expansion and reconstruction scoring

### Summary

Implemented another decode-focused pass to reduce hardcoded block-table assumptions and improve reconstruction candidate diagnostics.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added `selected_block_layout` to expose which block-table variant was selected.

- `src/vsf/unityfs_reader.cpp`
  - Reworked metadata table parse to evaluate multiple block layouts:
    - `be`, `be-swap-size`, `be-swap-flags`, `be-swap-size-flags`
    - `le`, `le-swap-size`, `le-swap-flags`, `le-swap-size-flags`
  - Added block-layout scoring heuristics and node-range consistency checks before selecting a layout.
  - Extended reconstruction attempts to track partial progress (`decoded_blocks`) and report best partial attempt.
  - Added per-block decode variants during reconstruction:
    - original
    - swapped size
    - swapped flag bytes
    - swapped size + swapped flag bytes
  - Enhanced block decode failure detail to include variant-level failure reasons.

- `src/avatar/vsfavatar_loader.cpp`
  - Included selected block layout in metadata warning output.

### Verified

- `Release` build succeeded.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`).
- Pipeline remains at `Compat: partial` / `Meshes: 0`; metadata stage is stable and now reports `block layout=...`, while reconstruction blocker is still concentrated at block 0.
- Latest fixed-set snapshot (`2026-03-02T23:24:05`) shows block-0 failures converged to:
  - `mode=0`, `code=DATA_BLOCK_READ_FAILED`
  - expected sizes observed: `74890067`, `88135067`, `125513796`, `402596`

## 2026-03-02 - VSFAvatar diagnostics hardening + NativeCore render-resource API extension

### Summary

Added stronger reconstruction diagnostics for `.vsfavatar` block decode failures and extended `nativecore` render API lifecycle for host-side wiring.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added block decode failure diagnostics:
    - `failed_block_index`
    - `failed_block_mode`
    - `failed_block_expected_size`
    - `failed_block_error_code`

- `src/vsf/unityfs_reader.cpp`
  - Added metadata candidate validation + scoring path to reduce fragile first-hit candidate selection.
  - Added block failure error-code mapping:
    - `DATA_BLOCK_READ_FAILED`
    - `DATA_BLOCK_RAW_MISMATCH`
    - `DATA_BLOCK_LZ4_FAIL`
    - `DATA_BLOCK_LZMA_UNIMPLEMENTED`
  - Added block-level failure context in reconstruction error text (`block`, `mode`, `expected`, `code`).
  - Added heuristic byte-order handling for block flags to improve compression-mode plausibility.

- `src/avatar/vsfavatar_loader.cpp`
  - Added warning emission for block diagnostics (`data block diagnostic: ...`).

- `include/vsfclone/nativecore/api.h`
  - Extended `NcRenderContext` with D3D11 handles:
    - `d3d11_device`
    - `d3d11_device_context`
    - `d3d11_rtv`
  - Added render-resource lifecycle APIs:
    - `nc_create_render_resources`
    - `nc_destroy_render_resources`

- `src/nativecore/native_core.cpp`
  - Added per-avatar render-resource readiness tracking.
  - Implemented lifecycle API stubs with handle validation.
  - Updated `nc_render_frame` validation to require D3D11 context handles and at least one render-ready avatar.

- `include/vsfclone/avatar/avatar_package.h`
  - Added future-facing render payload containers:
    - `mesh_payloads`
    - `material_payloads`
    - `texture_payloads`

### Verified

- `Release` build succeeded after API and parser updates.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`).
- Current samples still load as `Compat: partial`, with clearer blocker details now visible in diagnostics (`mode=1`, large expected block sizes, read/decode failure).

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

## 2026-03-02 - VSFAvatar phase 2 decompression hardening

### Summary

Hardened UnityFS metadata/data decompression and expanded diagnostics to accelerate blocker resolution.

### Changed

- `src/vsf/unityfs_reader.cpp`
  - Updated metadata-at-end detection to support sample variant flag usage.
  - Added LZ4 frame fallback decode path alongside raw LZ4 decode.
  - Added multi-mode decompression attempts (`block/header/LZ4/LZ4HC/raw`) for metadata and data blocks.
  - Added multi-strategy metadata handling:
    - whole compressed metadata attempt
    - 16-byte hash-prefix + compressed tail attempt
  - Added reconstruction diagnostics:
    - candidate attempt count
    - successful reconstruction offset
    - serialized parse fallback error code propagation

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added diagnostic fields:
    - `reconstruction_attempts`
    - `reconstruction_success_offset`
    - `serialized_parse_error_code`

- `src/avatar/vsfavatar_loader.cpp`
  - Added warnings for reconstruction attempts/success offset and serialized parse code.

- `README.md`
  - Added phase-2 decompression hardening summary and current blocker status.

### Verified

- Release build succeeded after decompression hardening.
- Sample probe report regenerated successfully (`build/reports/vsfavatar_probe.txt`).
- Current sample set still fails metadata decompression under in-house LZ4 logic, with improved explicit diagnostics.

## 2026-03-02 - VSFAvatar phase 2 diagnostics expansion (offset/strategy probing)

### Summary

Expanded metadata decode instrumentation and probing strategies to better isolate why sample bundles still fail metadata reconstruction.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added metadata decode diagnostics:
    - `metadata_offset`
    - `metadata_decode_strategy`
    - `metadata_decode_mode`
    - `metadata_decode_error_code`

- `include/vsfclone/vsf/serialized_file_reader.h`
  - Added parser metadata fields:
    - `parse_path`
    - `error_code`

- `src/vsf/serialized_file_reader.cpp`
  - Populated summary parse path metadata (`metadata-endian-little` / `metadata-endian-big`).
  - Improved combined parse failure string for dual-endian attempts.

- `src/vsf/unityfs_reader.cpp`
  - Added metadata offset candidate scan around file tail (16-byte aligned window).
  - Added metadata decode strategy attempts:
    - whole compressed decode
    - hash-prefix + compressed tail decode
    - raw-direct metadata parse fallback
  - Added mode candidate fallback and extended LZ4 fallback variants:
    - raw decode
    - frame decode
    - size-prefixed raw decode
  - Added bounded candidate error aggregation to keep diagnostics readable.

- `src/avatar/vsfavatar_loader.cpp`
  - Added warning fields for decode strategy/mode/offset and decode error code.

- `tools/vsfavatar_sample_report.ps1`
  - Added fixed baseline support:
    - `-UseFixedSet`
    - `-FixedSamples`

### Verified

- Release build succeeded after integration.
- Fixed sample report generation succeeded:
  - `build/reports/vsfavatar_probe_fixed.txt`
- Baseline sample set currently still reports metadata decode failure (`META_DECODE_FAILED`).

### Current blocker

- Despite broader probing and fallback paths, metadata decode for current `.vsfavatar` samples still fails in the in-house decoder path.
- This continues to block `object_table_parsed` on baseline samples.

## 2026-03-02 - VSFAvatar phase 2 metadata candidate refinement

### Summary

Refined metadata offset selection and reconstruction candidate wiring so sample bundles progress past metadata decode into reconstruction diagnostics.

### Changed

- `src/vsf/unityfs_reader.cpp`
  - merged metadata candidate sets from:
    - primary metadata offset root
    - header-adjacent offset root
  - added aligned tail-window metadata scanning in candidate generation
  - expanded metadata decode prefix attempts (`prefix-0..32`)
  - fixed reconstruction call to use actual parsed metadata offset (`probe.metadata_offset`)
  - added reconstruction candidates based on parsed metadata location:
    - `metadata_offset + compressed_metadata_size`
    - aligned variant
  - deduplicated reconstruction candidates before attempts

- `README.md`
  - added phase-2 refinement summary and updated blocker state

### Verified

- Release build succeeded after refinement.
- Fixed sample report regenerated successfully.
- Baseline samples now consistently reach metadata parse stage and fail at reconstruction stage with explicit errors.

### Current blocker

- Data block reconstruction still fails (`raw block size mismatch` / read failure) on baseline samples.
- `object_table_parsed` remains blocked until block decode interpretation is corrected.

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
