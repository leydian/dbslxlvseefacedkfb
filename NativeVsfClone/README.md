# VsfCloneNative

Native C++ scaffold for a standalone VTuber-style runtime with:

- `.vrm` input path handling (GLB/mesh payload MVP)
- `.vxavatar` input path handling (MVP parser + payload)
- `.vxa2` input path handling (manifest + TLV section decode MVP)
- `.xav2` input path handling (vxa2-derived container with mesh-render/material-params sections)
- `.vsfavatar` probing via UnityFS header parser (implemented)
- `nativecore.dll` C ABI for host/UI integration
- streaming and OSC runtime output path (`shared-memory frame sender`, `UDP OSC`)
- host app skeletons (`host/WpfHost`, `host/WinUiHost`) via shared `host/HostCore`

## VSFAvatar parser mode (current default)

`.vsfavatar` loading is now sidecar-first.

- Default mode: `VSF_PARSER_MODE=sidecar`
- In-house only mode: `VSF_PARSER_MODE=inhouse`
- Strict sidecar mode (no fallback): `VSF_PARSER_MODE=sidecar-strict`
- Sidecar binary override: `VSF_SIDECAR_PATH=...`
- Sidecar timeout override (ms): `VSF_SIDECAR_TIMEOUT_MS=15000`

If `sidecar` mode fails to execute:

- `sidecar`: fallback to in-house parser and append fallback warnings.
- `sidecar-strict`: return failure directly.

## What is implemented now

- `AvatarLoaderFacade` dispatches by extension (`.vrm`, `.vxavatar`, `.vxa2`, `.xav2`, `.vsfavatar`).
- `VsfAvatarLoader` reads UnityFS metadata and reports:
  - signature/version/player version/engine version
  - compression mode flags
  - basic token probes (`CAB-`, `VRM`) in early data window
  - serialized candidate fallback probing:
    - truncated node-window retries on partial reconstructed streams
    - all-node fallback when CAB/assets path hints are weak
    - bounded stream scan for SerializedFile-like headers
- `nativecore.dll` exports a stable API:
  - init/shutdown
  - avatar load/query/unload
  - tracking frame submission
  - render tick (D3D11 window-target mesh/material draw path)
  - Spout/OSC start-stop with runtime backends
  - runtime stats retrieval (`nc_get_runtime_stats`)
  - last error retrieval
- `host/HostCore` wraps C ABI calls for WPF/WinUI hosts.
- `host/HostCore` now includes a shared UI orchestration layer:
  - `HostController` (lifecycle/render/output coordination)
  - UI state snapshots (`HostSessionState`, `OutputState`, `DiagnosticsSnapshot`)
  - bounded operation log stream (`HostLogEntry`)
- `host/HostApps.sln` groups `HostCore`, `WpfHost`, and `WinUiHost`.
- Detailed implementation report:
  - `docs/reports/ui_host_runtime_integration_2026-03-02.md`
- Detailed operation-focused UI redesign report:
  - `docs/reports/ui_host_operation_redesign_2026-03-03.md`
- `vsfclone_cli` and `avatar_tool` print structured load diagnostics.
- `vrm_to_xav2` converts `.vrm` to `.xav2` using runtime payload extraction.
- `vsfavatar_sidecar` is built as an external parser process.
  - complete-state normalization:
    - when `probe_stage=complete` and `object_table_parsed=true`, `primary_error_code` is emitted as `NONE`
- VRM runtime payloads now include:
  - interleaved position/uv vertex payload extraction
  - material alpha mode/cutoff/double-sided metadata
  - texture payload upload and shader resource binding
- WPF diagnostics panel now shows:
  - per-frame render return code
  - per-avatar draw-call count
  - expression summary text
- WPF/WinUI host shells now provide:
  - operation sections (`Session`, `Avatar`, `Outputs`)
  - guarded actions with confirmation prompts for disruptive operations
  - state-based button enable/disable to enforce valid operation order
  - structured diagnostics views (`Runtime`, `Avatar`, `Logs`)
  - persistent status strip (session/avatar/render/frame/output/last-error)

## What is not implemented yet

- Full UnityFS block decompression and object table reconstruction
- SerializedFile class parsing (`GameObject`, `Mesh`, `Material`, etc.)
- Full VRM feature coverage (full MToon, SpringBone, expressions)
- `.vxavatar` manifest/material override parse/apply
- `.vxa2` streaming payload unpack optimization
- Full production renderer features (normal/tangent/skin pipeline, full MToon lighting, post process)
- Full Spout2 SDK interop compatibility (current sender uses internal shared memory transport)

## Build (Windows)

Prerequisites:

- Visual Studio 2022 Build Tools (MSVC)
- CMake 3.20+
- .NET 8 SDK (for `host/*` projects)

Commands:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run:

```powershell
.\build\Release\vsfclone_cli.exe "D:\dbslxlvseefacedkfb\개인??1-3.vsfavatar"
.\build\Release\avatar_tool.exe "D:\path\to\avatar.vxavatar"
.\build\Release\vrm_to_xav2.exe "D:\path\to\avatar.vrm" "D:\path\to\avatar.xav2"
.\build\Release\vsfavatar_sidecar.exe "D:\path\to\avatar.vsfavatar"
```

Optional parser mode controls:

```powershell
$env:VSF_PARSER_MODE = "sidecar"        # default
$env:VSF_PARSER_MODE = "inhouse"        # bypass sidecar
$env:VSF_PARSER_MODE = "sidecar-strict" # no fallback
$env:VSF_SIDECAR_PATH = "D:\custom\vsfavatar_sidecar.exe"
$env:VSF_SIDECAR_TIMEOUT_MS = "15000"
```

## GUI EXE Publish (WPF default, WinUI optional)

Prerequisites:

- .NET 8 SDK
- Windows App SDK tooling (WinUI host publish)
- `build/Release/nativecore.dll` available (native build)

Run (WPF only default):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1
```

Run (WPF + WinUI):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi
```

Outputs:

- `dist/wpf` (`WpfHost.exe` + `nativecore.dll`)
- `dist/winui` (`WinUiHost.exe` + `nativecore.dll`, only when `-IncludeWinUi` is set)
- `build/reports/host_publish_latest.txt`

Notes:

- Script auto-kills running `WpfHost`/`WinUiHost` before publish.
- If `build/Release/nativecore.dll` is locked, script falls back to `build_hotfix` and copies the fallback DLL.

## Unity XAV2 SDK scaffold

- Package path: `unity/Packages/com.vsfclone.xav2`
- Target: Unity `2022.3 LTS` (Built-in RP)
- Included:
  - runtime `.xav2` parser scaffold
  - editor exporter scaffold with strict shader policy list:
    - `lilToon`, `Poiyomi`, `potatoon`, `realtoon`

## VSFAvatar quality gate

Run fixed-set probe + gate evaluation:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
```

Gate rules:

- Gate A: all samples complete without process failure and required sidecar fields exist.
- Gate B: at least one sample reaches `failed-serialized` or `complete`.
- Gate C: when `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`, tuple evidence must exist:
  - `SidecarFailedReadOffset > 0`
  - `SidecarFailedCompressedSize > 0`
  - `SidecarFailedUncompressedSize > 0`
  - `SidecarOffsetFamily` must be non-empty
- Gate D: at least one sample must satisfy:
  - `SidecarProbeStage=complete`
  - `SidecarObjectTableParsed=True`
  - `SidecarPrimaryError` is `NONE` or empty

Exit code:

- `0`: all gates pass
- `1`: at least one gate fails (including Gate D strict fail)

Outputs:

- probe report: `build/reports/vsfavatar_probe_latest_after_gate.txt`
- gate summary: `build/reports/vsfavatar_gate_summary.txt`
- baseline compare input (default): `build/reports/vsfavatar_probe_fixed.txt`

## VRM quality gate

Run fixed profile (recommended default):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5
```

Run auto profile (sorted discovery top-5):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile auto5
```

Gate rules:

- Gate A: all selected VRM samples must load successfully.
- Gate B: each sample must satisfy:
  - `Format=VRM`
  - `ParserStage=runtime-ready`
  - `Compat` is not `failed`
  - `MeshPayloads > 0`
- Gate C: each sample must satisfy:
  - `MaterialPayloads > 0`
  - `TexturePayloads > 0`
- Gate D: each sample must satisfy:
  - `ExpressionCount > 0`

Exit code:

- `0`: all gates pass
- `1`: one or more gates fail

Outputs:

- `fixed5`:
  - probe report: `build/reports/vrm_probe_fixed5.txt`
  - gate summary: `build/reports/vrm_gate_fixed5.txt`
- `auto5`:
  - probe report: `build/reports/vrm_probe_auto5.txt`
  - gate summary: `build/reports/vrm_gate_auto5.txt`

## VXAvatar/VXA2 quality gate

Run quick profile (fixed + synthetic):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick
```

Run full profile (fixed + discovered real samples + synthetic):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -Profile full
```

Gate rules:

- Gate A: fixed `.vxavatar` sample must satisfy `Format=VXAvatar`, `Compat=full`, `ParserStage=runtime-ready`, `PrimaryError=NONE`, and payload counts (`MeshPayloads`, `TexturePayloads`) >= 1.
- Gate B: synthetic corrupted `.vxavatar` samples must stay non-crashing and classify as `Compat=failed|partial` with `PrimaryError=VX_SCHEMA_INVALID|VX_UNSUPPORTED_COMPRESSION`.
- Gate C: `.vxa2` fixed + corrupted coverage:
  - fixed `.vxa2` must keep `Format=VXA2` and valid parser stage (`parse|resolve|payload|runtime-ready`)
  - corrupted `.vxa2` must classify as `VXA2_SECTION_TRUNCATED|VXA2_SCHEMA_INVALID`
- Gate D: required output fields must exist for each sample (`InputKind`, `InputTag`, `Format`, `Compat`, `ParserStage`, `PrimaryError`).
- Gate E (full profile): `real-full` sample rows must remain parseable.
  - Optional strict mode: `-RequireRealFullSamples` to fail when no `real-full` rows are present.

Exit code:

- `0`: all gates pass
- `1`: one or more gates fail

Outputs:

- probe report: `build/reports/vxavatar_probe_latest.txt`
- gate summary: `build/reports/vxavatar_gate_summary.txt`
- gate summary JSON: `build/reports/vxavatar_gate_summary.json`
- synthetic inputs: `build/tmp_vx/demo_mvp_truncated.vxavatar`, `build/tmp_vx/demo_mvp_cd_mismatch.vxavatar`, `build/tmp_vx/demo_tlv_truncated.vxa2`

CI:

- `.github/workflows/vxavatar-gate.yml`
  - `quick-gate` job: strict PR gate (`-UseFixedSet -Profile quick`)
  - `full-gate` job: strict PR gate (`-Profile full`)
  - both jobs upload probe/summary artifacts for diagnosis

Sidecar JSON contract:

- loader accepts `schema_version=2|3` (current output: `3`)
- required fields: `status`, `display_name`, `extractor_version`, `object_table_parsed`, `primary_error_code`
- diagnostic fields: `probe_stage`, `selected_block_layout`, `selected_offset_family`, `reconstruction_summary`, `reconstruction_candidate_count`, `best_candidate_score`, `serialized_candidate_count`, `serialized_attempt_count`, `serialized_best_candidate_path`, `serialized_best_candidate_score`
- expected arrays: `warnings[]`, `missing_features[]`
- sidecar errors are surfaced with codes such as:
  - `SIDECAR_TIMEOUT`
  - `SIDECAR_EXEC_FAILED`
  - `SIDECAR_RUNTIME_ERROR`
  - `SCHEMA_INVALID`

Sidecar diagnostics semantics (schema v3):

- `probe_stage` values:
  - `header`: UnityFS header read
  - `metadata-candidate`: metadata offset probing
  - `metadata-parsed`: metadata table parse succeeded
  - `reconstruction`: block stream reconstruction in progress
  - `failed-reconstruction`: data block reconstruction failed
  - `serialized`: serialized node parse in progress
  - `failed-serialized`: serialized parse failed on candidates
  - `complete`: object table parsed successfully
- `primary_error_code`:
  - a single dominant blocker code for automation and dashboards
  - examples: `META_DECODE_FAILED`, `DATA_BLOCK_READ_FAILED`, `SF_NO_VALID_NODE_PARSED`
- `selected_offset_family`:
  - indicates which reconstruction offset candidate family won (or best-partial family on failure)
  - examples: `after-metadata`, `aligned-after-metadata`, `tail-packed`, `header-window`, `tail-window`
- `selected_block0_hypothesis` / `block0_attempt_count`:
  - records which block-0 hypothesis variant was selected (or last/best-partial failed hypothesis)
  - reports how many block-0 hypothesis attempts were executed
- `block0_selected_offset` / `block0_selected_mode_source`:
  - records which reconstruction start offset was associated with block-0 hypothesis selection
  - records whether selected mode came from `header-derived`, `block-flag`, or `fallback`
- `reconstruction_summary`:
  - dominant failure class aggregated across reconstruction attempts
  - currently converges to `DATA_BLOCK_READ_FAILED` on fixed baseline samples
- `reconstruction_candidate_count` / `best_candidate_score`:
  - exposes how many reconstruction offsets were tried and which candidate quality score won
- `failed_block_read_offset` / `failed_block_compressed_size` / `failed_block_uncompressed_size`:
  - pinpoints the block read location and size tuple tied to the dominant block-level failure
- `serialized_candidate_count` / `serialized_attempt_count`:
  - records how many serialized nodes were considered and how many offset-adjusted parse attempts were executed
- `serialized_best_candidate_path` / `serialized_best_candidate_score`:
  - records the highest scoring serialized candidate (node path + offset) and the candidate score used for tie-breaks

Latest behavior notes (2026-03-02):

- Sidecar stdout capture was hardened to avoid pipe deadlock when warnings are long.
- `sidecar-strict` now surfaces timeout/failure directly with structured error strings.
- `sidecar` mode preserves in-house fallback for operational continuity.
- Current parser blocker is still unchanged:
  - baseline samples remain `Compat: partial`, `Meshes: 0`
  - representative decode failure remains `DATA_BLOCK_READ_FAILED` at block 0

Latest behavior notes (2026-03-03):

- In-house and sidecar warning streams now use normalized prefixes:
  - `W_*` for stage/progress diagnostics
  - `E_*` for dominant failure diagnostics
- Sidecar contract now exposes stage + primary error directly:
  - `probe_stage`
  - `primary_error_code`
  - `selected_block_layout`
  - `selected_offset_family`
  - `reconstruction_summary`
- Fixed sample report was regenerated after this update:
  - `build/reports/vsfavatar_probe_latest_after_impl.txt`
- Result did not change yet for fixed baseline:
  - all 4 samples still end at `Compat: partial`, `Meshes: 0`
  - dominant blocker still `DATA_BLOCK_READ_FAILED` at block 0

Latest behavior notes (2026-03-03, block-0 hypothesis pass):

- Added block-0 focused decode hypotheses in the reconstruction stage:
  - `orig-trim16`, `orig-trim32`, `orig-clamp-range` (alongside existing size/flag swap variants)
- Added block-0 observability fields:
  - `selected_block0_hypothesis`
  - `block0_attempt_count`
- Sample report script now appends sidecar diagnostics per sample:
  - `SidecarProbeStage`
  - `SidecarPrimaryError`
  - `SidecarBlockLayout`
  - `SidecarOffsetFamily`
  - `SidecarBlock0Hypothesis`
  - `SidecarBlock0Attempts`
- Fixed baseline remains blocked after this pass:
  - all 4 fixed samples still report `failed-reconstruction`, `DATA_BLOCK_READ_FAILED`, `Meshes: 0`

Latest behavior notes (2026-03-03, VXAvatar MVP parse/payload pass):

- `.vxavatar` loader moved from signature-only scaffold to manifest/payload aware flow.
- Added parser lifecycle state and dominant error code propagation:
  - `parse -> resolve -> payload -> runtime-ready`
  - exposed via `parser_stage`, `primary_error_code`
- Added ZIP central-directory based entry resolution and manifest discovery:
  - `manifest.json` (root or nested path suffix)
  - path normalization and unsafe relative path rejection (`..`, absolute, drive-letter)
- Added MVP manifest contract (required):
  - `avatarId` (or `avatar_id`)
  - `meshRefs[]`
  - `materialRefs[]`
  - `textureRefs[]`
- Added payload mapping:
  - mesh entries -> `mesh_payloads[].vertex_blob`
  - material entries -> `material_payloads[]` (MToon placeholder policy)
  - texture entries -> `texture_payloads[]` with inferred format (`png/jpeg/tga/bmp/binary`)
- Current `.vxavatar` compression behavior:
  - ZIP `stored(0)` and `deflate(8)` are supported in-process
  - other ZIP methods return `VX_UNSUPPORTED_COMPRESSION` with `Compat: partial`
- NativeCore/CLI diagnostics expanded:
  - `NcAvatarInfo` now reports payload counts (`mesh/material/texture`)
  - `parser_stage`, `primary_error_code` are surfaced in `avatar_tool`

Latest behavior notes (2026-03-02, VXAvatar in-process deflate migration):

- Removed external PowerShell extractor fallback from `.vxavatar` payload reads.
- Added in-process `deflate(8)` decode path in `vxavatar_loader`.
- Updated payload failure classification:
  - unsupported ZIP method -> `VX_UNSUPPORTED_COMPRESSION`
  - malformed/truncated entry payload -> `VX_SCHEMA_INVALID`
- Verified runtime behavior on current samples:
  - `sample/demo_mvp.vxavatar` stays `Compat: full`, `ParserStage: runtime-ready`
  - no `VX_EXTERNAL_EXTRACTOR` warning emitted
- Detailed report:
  - `docs/reports/vxavatar_deflate_inprocess_2026-03-02.md`

Latest behavior notes (2026-03-03, VSFAvatar reconstruction scoring + failure offsets):

- Reconstruction candidate families were normalized:
  - `after-metadata`, `aligned-after-metadata`, `tail-packed`, `header-window`, `tail-window`
- Candidate windows were expanded to `+/-4096` bytes (`16`-byte step).
- Block-0 decode hypotheses now include:
  - `prefix-skip-16`, `prefix-skip-32`
- New diagnostics were added for observability:
  - `reconstruction_candidate_count`
  - `best_candidate_score`
  - `failed_block_read_offset`
  - `failed_block_compressed_size`
  - `failed_block_uncompressed_size`
- Implausible-size failures now map to:
  - `DATA_BLOCK_SIZE_IMPLAUSIBLE`
- Fixed-set report after this pass:
  - `build/reports/vsfavatar_probe_latest_after_scoring.txt`
- Fixed baseline result remains reconstruction-blocked:
  - all fixed samples: `SidecarProbeStage=failed-reconstruction`
  - dominant code: `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
  - dominant family: `SidecarOffsetFamily=aligned-after-metadata`
  - candidate count range observed: `791..1057`
  - best score observed: `10`
  - representative failure tuple:
    - `SidecarFailedReadOffset=4250`
    - `SidecarFailedCompressedSize=14778976`
    - `SidecarFailedUncompressedSize=74890067`

Latest behavior notes (2026-03-03, VSFAvatar stage-lift gate pass):

- Reconstruction candidate tie-break was aligned to:
  - `decoded_blocks` > `score` > family priority (`after-metadata` first)
- On reconstruction failure, serialized probing now runs against best-partial stream.
- Primary error remains reconstruction-dominant when present:
  - `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
- Fixed-set gate report was regenerated:
  - `build/reports/vsfavatar_probe_latest_after_scoring.txt`
- Gate outcome (fixed 4 samples):
  - `GateA_NoCrashAndDiagPresent=PASS`
  - `GateB_AtLeastOneFailedSerializedOrComplete=PASS`
  - `GateC_ReadFailureHasOffsetModeSizeEvidence=PASS`
- Current stage status:
  - all fixed samples now report `SidecarProbeStage=failed-serialized`
  - dominant offset family is `after-metadata`

Latest behavior notes (2026-03-03, VSFAvatar serialized-candidate expansion + GateD):

- Serialized candidate probing now includes bounded offset deltas per node:
  - `0`, `+16`, `-16`, `+32`, `-32`, `+64`, `-64`
- Added serialized-candidate diagnostics in sidecar/loader:
  - `serialized_candidate_count`
  - `serialized_attempt_count`
  - `serialized_best_candidate_path`
  - `serialized_best_candidate_score`
- Fixed-set report generation now hard-fails on incomplete fixed sample set.
- Sample report now emits `SidecarObjectTableParsed` and serialized diagnostics per sample.
- Quality gate now enforces Gate D (`complete + object_table_parsed + no primary error`) as strict fail criteria.

Latest behavior notes (2026-03-03, VRM minimal runtime-ready slice):

- `.vrm` loader moved from scaffold-only behavior to a minimal GLB payload path:
  - GLB v2 header/chunk parse (`JSON` + `BIN`)
  - glTF mesh extraction for `POSITION` + `indices`
  - runtime payload population into:
    - `mesh_payloads`
    - `material_payloads` (minimal placeholder material)
- Parser diagnostics now use staged contract:
  - `parse -> resolve -> payload -> runtime-ready`
  - `VRM_SCHEMA_INVALID`, `VRM_ASSET_MISSING`, `NONE`
- NativeCore render path is no longer pure placeholder:
  - `nc_create_render_resources` now validates mesh payload availability
  - `nc_render_frame` performs minimal D3D11 clear on provided RTV
- Sample validation snapshot:
  - `sample/개인작08.vrm`: `Compat=full`, `ParserStage=runtime-ready`, `MeshPayloads=9`
  - `sample/Kikyo_FT Variant.vrm`: `Compat=full`, `ParserStage=runtime-ready`, `MeshPayloads=22`

Latest behavior notes (2026-03-02, VXA2 TLV section decode MVP):

- `.vxa2` loader now decodes section table entries after manifest:
  - section header: `type(u16)`, `flags(u16)`, `size(u32)`
  - known types: mesh blob (`0x0001`), texture blob (`0x0002`), material override (`0x0003`)
- Added strict section boundary validation:
  - truncated section headers/payloads now map to `VXA2_SECTION_TRUNCATED`
- Added payload/reference gap classification:
  - missing mesh/texture payloads now map to `VXA2_ASSET_MISSING`
  - loader keeps `Compat: partial` when parse succeeds but coverage is incomplete
- Added format-level diagnostics in NativeCore/CLI:
  - `format_section_count`
  - `format_decoded_section_count`
  - `format_unknown_section_count`
- Detailed report:
  - `docs/reports/vxa2_tlv_update_2026-03-02.md`

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

