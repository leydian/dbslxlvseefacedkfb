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
- Render resource lifecycle: `nc_create_render_resources`, `nc_destroy_render_resources`
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

## Recent implementation summary (2026-03-02, phase 2 continuation)

Implemented the first full pipeline attempt for VSFAvatar object discovery:

- Added `SerializedFileReader` module (`include/vsfclone/vsf/serialized_file_reader.h`, `src/vsf/serialized_file_reader.cpp`).
- Extended `UnityFsProbe` with object-table diagnostics:
  - `object_table_parsed`, `object_count`
  - `mesh_object_count`, `material_object_count`, `texture_object_count`
  - `game_object_count`, `skinned_mesh_renderer_count`
  - `major_types_found`
- Extended `UnityFsReader` pipeline:
  - metadata table parse -> data block reconstruction attempt -> serialized node parse attempt.
- Updated `VsfAvatarLoader` diagnostics:
  - always reports metadata/serialized diagnostic failures when present.
  - maps discovered object counts into placeholder mesh/material entries when available.
  - `missing_features` now distinguishes object-discovery and payload-extraction stages.
- Added sample report script:
  - `tools/vsfavatar_sample_report.ps1`

Current status:

- Metadata parsing is stable on tested samples.
- Object table parsing path is wired but blocked by bundle data decompression failure on current samples (`LZ4 decode failed` on reconstructed data candidates).

## Recent implementation summary (2026-03-02, phase 2 decompression hardening)

Implemented additional decompression hardening and diagnostics for the VSFAvatar pipeline:

- Added multi-strategy metadata decompression attempts:
  - whole blob decode attempt
  - hash-prefix (16-byte) + compressed tail attempt
  - mode fallback tries (`header/block`, `LZ4`, `LZ4HC`, `raw`)
- Added LZ4 frame decode fallback path in addition to raw block decode.
- Added pipeline diagnostics fields in `UnityFsProbe`:
  - `reconstruction_attempts`
  - `reconstruction_success_offset`
  - `serialized_parse_error_code`
- Updated loader warning output to include reconstruction attempts/offset and serialized parse code.

Current blocker after hardening:

- Metadata decompression for current `.vsfavatar` samples still fails under current in-house LZ4 decoder logic.
- This blocks downstream `object_table_parsed` from turning true on those samples.

## Recent implementation summary (2026-03-02, phase 2 diagnostics expansion)

Implemented another parser iteration focused on metadata decode observability and offset/strategy exploration:

- Extended `UnityFsProbe` with metadata-level diagnostics:
  - `metadata_offset`
  - `metadata_decode_strategy`
  - `metadata_decode_mode`
  - `metadata_decode_error_code`
- Extended `SerializedFileSummary` with parser metadata:
  - `parse_path`
  - `error_code`
- Extended metadata decode attempts in `UnityFsReader`:
  - metadata offset candidate generation around tail-window with 16-byte alignment scan
  - multiple metadata decode strategies (`whole`, `hash-prefix`, `raw-direct`)
  - decode mode fallback candidates (`block/header/LZ4/LZ4HC/raw`)
  - LZ4 raw/frame/size-prefixed fallback attempts
- Added bounded diagnostics aggregation:
  - retains representative offset failures and truncates excessive candidate error spam.
- Updated loader warnings:
  - includes metadata decode strategy/mode/offset and decode error code.
- Updated sample report script:
  - added `-UseFixedSet` and fixed 4-sample baseline set for repeatable checks.

Current status after this iteration:

- Build remains stable.
- Fixed sample report execution is stable.
- Current sample baseline still reports `META_DECODE_FAILED`, so object table extraction remains blocked.

## Recent implementation summary (2026-03-02, phase 2 metadata candidate/refinement pass)

Implemented a focused refinement pass to move failure downstream from metadata decode to data-block reconstruction:

- `UnityFsReader` metadata candidate logic updated:
  - metadata offset candidates are now merged from both primary offset and header-adjacent offset roots
  - tail-window aligned scanning is included for metadata-at-end ambiguity
- metadata decode strategy attempts expanded:
  - prefix brute-force path (`prefix-0` through `prefix-32`)
  - existing whole/hash-prefix/raw-direct strategies kept as fallbacks
- reconstruction stage wiring corrected:
  - reconstruction now uses the actual parsed metadata offset (`probe.metadata_offset`)
  - reconstruction start candidates now include `metadata_offset + compressed_metadata_size`
  - candidate offsets are deduplicated before decode attempts

Validation outcome:

- Metadata parse now succeeds on baseline samples (`decode strategy=prefix-0`, metadata offset discovered).
- Pipeline advances to reconstruction stage consistently.
- Current blocker has shifted to data-block decode mismatch (`raw block size mismatch` / read failure), which is now explicitly visible in diagnostics.

## Recent implementation summary (2026-03-02, diagnostics hardening + render API extension)

Implemented two focused updates:

- VSFAvatar reconstruction diagnostics hardening:
  - added block-level failure diagnostics (`failed_block_index/mode/expected/error_code`) to `UnityFsProbe`
  - added metadata candidate validation + scoring to reduce fragile first-hit selection
  - added explicit loader warnings for block diagnostics (`data block diagnostic: ...`)
- NativeCore render API extension:
  - `NcRenderContext` now includes D3D11 pointers (`d3d11_device`, `d3d11_device_context`, `d3d11_rtv`)
  - added `nc_create_render_resources` and `nc_destroy_render_resources` for host-managed render lifecycle

Current status after this pass:

- Fixed sample set still reports `Compat: partial`, `Meshes: 0`.
- Blocker is clearer: reconstruction currently fails at block 0 with `mode=1` and implausibly large expected uncompressed sizes on baseline samples, indicating remaining interpretation mismatch in block table/decode stage.

### Detailed code-level update

- UnityFS probe/diagnostics
  - `include/vsfclone/vsf/unityfs_reader.h`
    - added block failure fields (`failed_block_index`, `failed_block_mode`, `failed_block_expected_size`, `failed_block_error_code`)
  - `src/vsf/unityfs_reader.cpp`
    - metadata candidate path now validates parsed tables and scores candidates before selection
    - reconstruction failure path now emits structured error codes for each block
    - block flag byte-order heuristic added for mode plausibility checks
- Loader warning behavior
  - `src/avatar/vsfavatar_loader.cpp`
    - emits explicit `data block diagnostic: ...` warnings so `avatar_tool`/`vsfclone_cli` show root-cause context directly
- NativeCore C ABI extension for renderer wiring
  - `include/vsfclone/nativecore/api.h`
    - `NcRenderContext` extended with:
      - `d3d11_device`
      - `d3d11_device_context`
      - `d3d11_rtv`
    - new APIs:
      - `nc_create_render_resources`
      - `nc_destroy_render_resources`
  - `src/nativecore/native_core.cpp`
    - added per-avatar render-ready state tracking
    - `nc_render_frame` now validates D3D11 pointers and render-ready avatar presence
- Avatar package forward-compat payload slots
  - `include/vsfclone/avatar/avatar_package.h`
    - added `mesh_payloads`, `material_payloads`, `texture_payloads` containers for upcoming extraction stages

### Quick verification commands

```powershell
cmake --build build --config Release
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_sample_report.ps1 -SampleDir ..\sample -OutputPath .\build\reports\vsfavatar_probe_latest.txt -UseFixedSet
Get-Content .\build\reports\vsfavatar_probe_latest.txt
```

## Recent implementation summary (2026-03-02, block-layout candidate expansion)

Implemented an additional decode pass focused on block-table interpretation variability:

- Added `selected_block_layout` to `UnityFsProbe` and surfaced it in loader warnings.
- Expanded metadata block-table candidate parsing to cover BE/LE + size-order + flag-byte variants.
- Added candidate scoring and node-range consistency checks before layout selection.
- Added reconstruction partial-progress diagnostics (`best partial decoded blocks`) and block-level variant decode attempts.

Current status after this pass:

- Metadata stage remains stable on fixed samples and now reports selected block layout.
- Fixed sample set still reports `Compat: partial`, `Meshes: 0`.
- Remaining blocker is still block 0 reconstruction/decode mismatch on baseline `.vsfavatar` files.

### Fixed-sample snapshot (2026-03-02T23:24:05)

From `build/reports/vsfavatar_probe_latest.txt`:

- `NewOnYou.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=74890067, code=DATA_BLOCK_READ_FAILED`
- `Character vywjd.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=88135067, code=DATA_BLOCK_READ_FAILED`
- `PPU (2).vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=125513796, code=DATA_BLOCK_READ_FAILED`
- `VRM dkdlrh.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=402596, code=DATA_BLOCK_READ_FAILED`

## Recent implementation summary (2026-03-02, reconstruction window expansion + block-total diagnostics)

Implemented an additional reconstruction diagnostics pass:

- Added new probe fields:
  - `total_block_compressed_size`
  - `total_block_uncompressed_size`
  - `reconstruction_best_partial_blocks`
- Expanded reconstruction start-offset candidates:
  - existing anchors + aligned variants
  - `+/-256` byte windows (16-byte steps) around key anchors
  - tail-packed anchor (`bundle_file_size - total_compressed`)
- Added bounded variant-level decode error aggregation for block-level diagnostics.
- Added LZ4 bounded fallback path after exact/frame/size-prefixed attempts fail.

Current status after this pass:

- Metadata diagnostics now show block totals and best partial reconstruction count.
- Fixed sample set still reports `Compat: partial`, `Meshes: 0`.
- Block-0 failure remains consistent across fixed samples (`mode=0`, `DATA_BLOCK_READ_FAILED`).

### Fixed-sample snapshot (2026-03-02T23:30:48)

From `build/reports/vsfavatar_probe_latest.txt`:

- `NewOnYou.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=74890067, code=DATA_BLOCK_READ_FAILED`
- `Character vywjd.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=88135067, code=DATA_BLOCK_READ_FAILED`
- `PPU (2).vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=125513796, code=DATA_BLOCK_READ_FAILED`
- `VRM dkdlrh.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=0, expected=402596, code=DATA_BLOCK_READ_FAILED`

## Recent implementation summary (2026-03-02, recon summary-code + count-endian probing)

Implemented another decode-focused pass in the in-house parser path:

- Added reconstruction summary diagnostics:
  - `selected_reconstruction_layout`
  - `reconstruction_failure_summary_code`
- Added reconstruction failure-code aggregation across offset attempts to surface dominant failure class.
- Expanded metadata table hypothesis space with count-endian probing:
  - block-count `BE/LE`
  - node-count `BE/LE`
- Adjusted layout scoring to penalize implausible `mode=0` raw-size interpretations.

Current status after this pass:

- Build remains stable.
- Fixed sample set still reports `Compat: partial`, `Meshes: 0`.
- Block-0 reconstruction failure remains dominant with `DATA_BLOCK_READ_FAILED`.

### Fixed-sample snapshot (2026-03-02T23:40:51)

From `build/reports/vsfavatar_probe_latest.txt`:

- `NewOnYou.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=1, expected=74890067, code=DATA_BLOCK_READ_FAILED`
- `Character vywjd.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=1, expected=88135067, code=DATA_BLOCK_READ_FAILED`
- `PPU (2).vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=1, expected=125513796, code=DATA_BLOCK_READ_FAILED`
- `VRM dkdlrh.vsfavatar`
  - `Compat: partial`, `Meshes: 0`, `LastWarning: index=0, mode=1, expected=402596, code=DATA_BLOCK_READ_FAILED`
