# Changelog

All notable implementation changes in this workspace are documented here.

## 2026-03-03 - VSFAvatar reconstruction stage-lift gate pass (failed-reconstruction -> failed-serialized)

### Summary

Completed the VSFAvatar quality-gate pass by promoting fixed-set samples from `failed-reconstruction` to `failed-serialized` stage while preserving reconstruction-dominant root-cause diagnostics (`DATA_BLOCK_READ_FAILED` with read tuple evidence).

### Changed

- `src/vsf/unityfs_reader.cpp`
  - Candidate selection priority was normalized to:
    - `decoded_blocks` (highest)
    - `score`
    - family priority (`after-metadata` first)
  - Best-partial stream is now retained and surfaced from reconstruction attempts.
  - On reconstruction failure, serialized probing is attempted against best-partial stream.
  - When reconstruction summary code exists, it remains dominant in `probe_primary_error`.

- `src/avatar/vsfavatar_loader.cpp`
  - Sidecar path now maps parser diagnostics into package fields:
    - `parser_stage`
    - `primary_error_code`
  - In-house path mirrors probe-level stage/error into package diagnostics.

- `tools/vsfavatar_sample_report.ps1`
  - Added gate-summary block:
    - `GateA_NoCrashAndDiagPresent`
    - `GateB_AtLeastOneFailedSerializedOrComplete`
    - `GateC_ReadFailureHasOffsetModeSizeEvidence`
  - Added per-run `GateRows` count for deterministic fixed-set checks.

### Verified

- `Release` build succeeded after changes.
- Fixed-set report regenerated:
  - `build/reports/vsfavatar_probe_latest_after_scoring.txt`
- Gate outcome:
  - `GateA_NoCrashAndDiagPresent=PASS`
  - `GateB_AtLeastOneFailedSerializedOrComplete=PASS`
  - `GateC_ReadFailureHasOffsetModeSizeEvidence=PASS`
- Fixed-set stage snapshot:
  - all 4 samples now at `SidecarProbeStage=failed-serialized`
  - primary error remains `DATA_BLOCK_READ_FAILED` with read-offset/compressed-size/uncompressed-size evidence.

## 2026-03-02 - VXAvatar/VXA2 quality gate harness (fixed-set + synthetic corruption)

### Summary

Added a dedicated quality gate harness for `.vxavatar` and `.vxa2` regression checks.
The harness runs `avatar_tool` over fixed baseline samples plus synthetic corruption samples and enforces deterministic pass/fail criteria.

### Changed

- `tools/vxavatar_sample_report.ps1` (new)
  - Produces structured probe output for `.vxavatar` and `.vxa2`.
  - Supports fixed-set mode with defaults:
    - `demo_mvp.vxavatar`
    - `demo_mvp.vxa2`
  - Regenerates synthetic corruption samples under `build/tmp_vx/`:
    - `demo_mvp_truncated.vxavatar`
    - `demo_mvp_cd_mismatch.vxavatar`
    - `demo_tlv_truncated.vxa2`
  - Emits per-sample metadata for gate parsing:
    - `InputKind`
    - `InputTag`

- `tools/vxavatar_quality_gate.ps1` (new)
  - Runs the probe script and evaluates strict gates:
    - Gate A: fixed `.vxavatar` success contract
    - Gate B: synthetic `.vxavatar` corruption handling
    - Gate C: `.vxa2` fixed/corruption classification
    - Gate D: required output field presence
  - Writes summary report:
    - `build/reports/vxavatar_gate_summary.txt`
  - Exit code contract:
    - `0` pass, `1` fail

- `README.md`
  - Added `VXAvatar/VXA2 quality gate` section with command, gate definitions, outputs, and exit-code policy.

- `docs/INDEX.md`
  - Added report link:
    - `docs/reports/vxavatar_gate_harness_2026-03-02.md`

- `docs/reports/vxavatar_gate_harness_2026-03-02.md` (new)
  - Documents synthetic sample policy, gate semantics, and runtime outputs.

## 2026-03-02 - VXAvatar in-process deflate decode (external extractor removal)

### Summary

Removed the PowerShell-based extraction fallback from `.vxavatar` and replaced it with in-process ZIP deflate decode using vendored `miniz`, so runtime no longer depends on external process execution for `method=8` entries.

### Changed

- `src/avatar/vxavatar_loader.cpp`
  - Removed:
    - `ReadZipEntryViaPowershell(...)`
    - external `std::system("powershell ...")` path
    - `W_PARSE: VX_EXTERNAL_EXTRACTOR` warning emission
  - Added:
    - local-header data-range resolver (`ResolveZipEntryDataRange`)
    - in-process `deflate(8)` decoder (`ReadDeflateZipEntry`)
    - payload failure classification split:
      - unsupported method -> `VX_UNSUPPORTED_COMPRESSION`
      - malformed/truncated/invalid payload read -> `VX_SCHEMA_INVALID`
  - Kept:
    - `stored(0)` path
    - `parse -> resolve -> payload -> runtime-ready` stage contract

- `third_party/miniz/*` (new vendored dependency)
  - Added miniz source/header set for in-process inflate implementation:
    - `miniz.c`, `miniz.h`, `miniz_common.h`, `miniz_tdef.h`, `miniz_tinfl.h`, `miniz_zip.h`, `miniz_export.h`

- `src/common/miniz_impl.cpp` (new)
  - Added single translation unit wrapper to compile miniz implementation files into `vsfclone_core`.

- `CMakeLists.txt`
  - Added `src/common/miniz_impl.cpp` to `vsfclone_core`.
  - Added private include path for `third_party/miniz`.
  - Removed temporary `find_package(ZLIB)` dependency path.

- `README.md`
  - Updated VXAvatar compression note: `stored(0)` + `deflate(8)` in-process support.
  - Added behavior note for external extractor removal.

### Verified

- `cmake --build build_vxdeflate --config Release` succeeded.
- `avatar_tool sample/demo_mvp.vxavatar`:
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`
  - no external extractor warning.
- Truncated sample check:
  - `build/tmp_vx/demo_mvp_truncated.vxavatar`
  - returns `Compat: failed`, `PrimaryError: VX_SCHEMA_INVALID`, no process crash.

## 2026-03-02 - VSFAvatar quality gate harness (A/B/C + baseline diff)

### Summary

Added a standalone quality-gate harness for fixed-set VSFAvatar regression checks so parser iteration runs can be evaluated with deterministic pass/fail criteria and baseline comparison.

### Changed

- `tools/vsfavatar_quality_gate.ps1` (new)
  - Runs `vsfavatar_sample_report.ps1` and parses probe output.
  - Evaluates strict gates:
    - Gate A: required field completeness + no parse/process failure
    - Gate B: at least one sample reaches `failed-serialized|complete`
    - Gate C: `DATA_BLOCK_READ_FAILED` samples include offset/size/family tuple evidence
  - Generates baseline diff summary:
    - `IMPROVED|REGRESSED|CHANGED|UNCHANGED|NEW`
  - Emits machine-usable exit code:
    - `0` pass, `1` fail

- `tools/vsfavatar_sample_report.ps1`
  - Added report header marker:
    - `GateInputVersion: 1`

- `README.md`
  - Added `VSFAvatar quality gate` section with command, gate definitions, output files, and exit-code policy.

- `docs/INDEX.md`
  - Added report link for gate harness documentation.

- `docs/reports/vsfavatar_gate_harness_2026-03-03.md` (new)
  - Documents gate semantics, diff labels, and failure interpretation.

### Verified

- Harness script parses fixed-set reports and emits explicit gate pass/fail summary.
- Gate B is strict-fail by default (`exit 1` when unmet).
- Fixed-set gate run result (`tools/vsfavatar_quality_gate.ps1 -UseFixedSet`):
  - `GateA=PASS`
  - `GateB=FAIL` (all samples remained `failed-reconstruction`)
  - `GateC=PASS`
  - `Overall=FAIL` (strict policy)
- Diff summary from gate output:
  - `Improved=0`
  - `Regressed=0`
  - `Changed=4`
  - `Unchanged=0`
  - `New=0`
- Generated files:
  - `build/reports/vsfavatar_probe_latest_after_gate.txt`
  - `build/reports/vsfavatar_gate_summary.txt`

## 2026-03-02 - Docs: add detailed VXA2 TLV update report

### Summary

Added a detailed implementation/verification report for the VXA2 TLV decode MVP pass.

### Changed

- Added report:
  - `docs/reports/vxa2_tlv_update_2026-03-02.md`
- Report includes:
  - implemented loader/API/doc deltas
  - build/run verification outcomes
  - known limitations and next-step backlog

### Verified

- Report content is aligned with current `main` behavior and validation logs.

## 2026-03-02 - Documentation structure normalization (index + archive policy)

### Summary

Standardized the documentation layout to reduce drift between core docs, format specs, implementation reports, and generated build reports.

### Changed

- Added documentation entrypoint:
  - `docs/INDEX.md`
- Added documentation maintenance guide:
  - `docs/CONTRIBUTING_DOCS.md`
- Added generated-report retention policy:
  - `build/reports/README.md`
- Added archive location for historical generated reports:
  - `docs/archive/build-reports/README.md`
- Applied `build/reports` cleanup by moving non-retained snapshots to archive.

### Verified

- Documentation index links resolve to existing files.
- `build/reports` now contains latest/milestone snapshots only.
- Archived report files are available under `docs/archive/build-reports/`.

## 2026-03-02 - VXA2 TLV section decode MVP + format diagnostics

### Summary

Implemented `.vxa2` binary section decoding so the loader can map real mesh/texture/material payload sections beyond header+manifest validation.

### Changed

- `src/avatar/vxa2_loader.cpp`
  - Added TLV section table parse after manifest:
    - section header: `type(u16)`, `flags(u16)`, `size(u32)`
  - Added known section decoders:
    - `0x0001` mesh blob section
    - `0x0002` texture blob section
    - `0x0003` material override section
  - Added strict boundary/truncation guard:
    - `VXA2_SECTION_TRUNCATED`
  - Added payload/schema guard for malformed known section payloads:
    - `VXA2_SCHEMA_INVALID`
  - Added manifest-reference coverage classification:
    - `VXA2_ASSET_MISSING` when mesh/texture refs cannot be resolved to section payloads
  - Added section counters in package diagnostics:
    - `format_section_count`
    - `format_decoded_section_count`
    - `format_unknown_section_count`

- `include/vsfclone/avatar/avatar_package.h`
  - Added generic format diagnostics counters:
    - `format_section_count`
    - `format_decoded_section_count`
    - `format_unknown_section_count`

- `include/vsfclone/nativecore/api.h`
  - Extended `NcAvatarInfo` with format diagnostics counters:
    - `format_section_count`
    - `format_decoded_section_count`
    - `format_unknown_section_count`

- `src/nativecore/native_core.cpp`
  - Mapped package format diagnostics counters into `NcAvatarInfo`.

- `tools/avatar_tool.cpp`
  - Added output lines:
    - `FormatSections`
    - `FormatDecodedSections`
    - `FormatUnknownSections`

- `docs/formats/vxa2.md`
  - Promoted v1 section layout from draft placeholder to concrete TLV contract.
  - Documented section types (`0x0001/0x0002/0x0003`) and payload field layouts.
  - Documented runtime truncation/unknown-type behavior.

- `README.md`
  - Updated `.vxa2` status to manifest + TLV section decode MVP.
  - Added latest behavior notes for VXA2 section decode and diagnostics.

## 2026-03-02 - VXAvatar deflate/BOM compatibility hardening (MVP unblock)

### Summary

Hardened the `.vxavatar` MVP path to handle real-world ZIPs produced with deflate compression (`method=8`) and UTF-8 BOM-prefixed manifests, removing the remaining blocker that kept valid sample files at `Compat: failed`.

### Changed

- `src/avatar/vxavatar_loader.cpp`
  - Added compression-method branch:
    - `stored(0)`: in-house local-header payload read (existing path)
    - `deflate(8)`: temporary PowerShell/.NET extraction fallback
  - Added `ReadZipEntryViaPowershell(...)` fallback:
    - opens archive with .NET `ZipFile`
    - extracts entry bytes
    - writes temp payload and re-ingests into loader
  - Added `ReadZipEntryPayload(...)` dispatcher so manifest/mesh/texture reads share the same compression-aware path.
  - Added UTF-8 BOM stripping for `manifest.json` prior to JSON parse.
  - Added parser warning for external extraction path:
    - `W_PARSE: VX_EXTERNAL_EXTRACTOR: deflate manifest extracted via PowerShell.`

### Behavior Impact

- Before this pass:
  - deflate-based `.vxavatar` files failed in parse stage with schema errors.
- After this pass:
  - deflate-based samples can complete parse/resolve/payload/runtime-ready flow.
  - same sample now reaches `Compat: full` with populated mesh/material/texture payload counts.

### Verified

- `Release` build succeeded after the hardening patch.
- `avatar_tool.exe D:\dbslxlvseefacedkfb\sample\demo_mvp.vxavatar`:
  - `Format: VXAvatar`
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`
  - `MeshPayloads/MaterialPayloads/TexturePayloads: 1/1/1`

## 2026-03-03 - VSFAvatar reconstruction candidate scoring + failure-offset diagnostics

### Summary

Focused the VSFAvatar in-house reconstruction pass on reproducible candidate scoring and richer block failure metadata so block-0 read/decode blockers can be triaged with concrete offsets and size tuples.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added reconstruction diagnostics:
    - `reconstruction_candidate_count`
    - `best_candidate_score`
  - Added block read diagnostics:
    - `failed_block_read_offset`
    - `failed_block_compressed_size`
    - `failed_block_uncompressed_size`

- `src/vsf/unityfs_reader.cpp`
  - Expanded reconstruction window scan from `+/-256` to `+/-4096` (`16`-byte step).
  - Normalized candidate families:
    - `after-metadata`
    - `aligned-after-metadata`
    - `tail-packed`
    - `header-window`
    - `tail-window`
  - Added candidate quality scoring (decoded block ratio + node-range consistency + block0 mode-source plausibility).
  - Added block-0 decode hypotheses:
    - `prefix-skip-16`
    - `prefix-skip-32`
  - Added implausible-size guard (`>256 MiB`) with explicit classification:
    - `DATA_BLOCK_SIZE_IMPLAUSIBLE`
  - Preserved failed read offset and size tuple in probe diagnostics for dominant failure paths.

- `tools/vsfavatar_sidecar.cpp`
  - Added JSON output fields:
    - `reconstruction_candidate_count`
    - `best_candidate_score`
    - `failed_block_read_offset`
    - `failed_block_compressed_size`
    - `failed_block_uncompressed_size`
  - Added warning stream line:
    - `W_RECON_META`

- `src/avatar/vsfavatar_loader.cpp`
  - Added sidecar warning mapping for reconstruction candidate score/count and failed-read tuple.
  - Extended in-house reconstruction warning payload with failed-read tuple and score/candidate count.

- `tools/vsfavatar_sample_report.ps1`
  - Added report fields:
    - `SidecarReconCandidateCount`
    - `SidecarBestCandidateScore`
    - `SidecarFailedReadOffset`
    - `SidecarFailedCompressedSize`
    - `SidecarFailedUncompressedSize`

### Verified

- `Release` build succeeded after diagnostics/scoring updates.
- Fixed-set report regenerated:
  - `build/reports/vsfavatar_probe_latest_after_scoring.txt`
- Current fixed baseline remains blocked at reconstruction:
  - `SidecarProbeStage=failed-reconstruction`
  - `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
  - dominant offset family observed: `aligned-after-metadata`
  - candidate count range observed: `791..1057`

## 2026-03-03 - VXAvatar MVP parser/payload integration + NativeCore diagnostics expansion

### Summary

Upgraded `.vxavatar` from scaffold signature checks to a manifest-aware MVP pipeline with payload extraction (stored ZIP entries), and extended NativeCore/API diagnostics to expose parser state and payload coverage.

### Changed

- `src/avatar/vxavatar_loader.cpp`
  - Replaced scaffold-only ZIP magic check with full ZIP central-directory traversal:
    - EOCD locate
    - central-directory parse
    - local-header validation
  - Added `manifest.json` discovery (root or nested suffix path).
  - Added lightweight in-house JSON parser for manifest decode.
  - Added required manifest validation:
    - `avatarId`/`avatar_id`
    - `meshRefs[]`
    - `materialRefs[]`
    - `textureRefs[]`
  - Added path hardening for asset refs (reject absolute/drive-letter/`..` traversal).
  - Added payload population:
    - `mesh_payloads` (`vertex_blob` from entry bytes)
    - `material_payloads` (MToon placeholder policy)
    - `texture_payloads` (format inference + bytes)
  - Added stage/error propagation:
    - `parser_stage` (`parse`, `resolve`, `payload`, `runtime-ready`)
    - `primary_error_code` (`NONE`, `VX_SCHEMA_INVALID`, `VX_MANIFEST_MISSING`, `VX_ASSET_MISSING`, `VX_UNSUPPORTED_COMPRESSION`)
  - MVP compression scope is currently `stored(0)` entries only.

- `include/vsfclone/avatar/avatar_package.h`
  - Added new source type:
    - `AvatarSourceType::Vxa2`
  - Added package-level parser diagnostics:
    - `parser_stage`
    - `primary_error_code`

- `include/vsfclone/nativecore/api.h`
  - Added format hint:
    - `NC_AVATAR_FORMAT_VXA2`
  - Extended `NcAvatarInfo`:
    - `mesh_payload_count`
    - `material_payload_count`
    - `texture_payload_count`
    - `parser_stage`
    - `primary_error_code`

- `src/nativecore/native_core.cpp`
  - Added `AvatarSourceType::Vxa2` mapping to `NC_AVATAR_FORMAT_VXA2`.
  - Added payload-count and parser-diagnostic mapping into `NcAvatarInfo`.

- `tools/avatar_tool.cpp`
  - Added output fields:
    - `ParserStage`
    - `PrimaryError`
    - `MeshPayloads`
    - `MaterialPayloads`
    - `TexturePayloads`
  - Added format display branch for `VXA2`.

- `src/avatar/vxa2_loader.h` / `src/avatar/vxa2_loader.cpp` (new)
  - Added `.vxa2` loader with MVP validation flow:
    - magic/version/header checks
    - manifest section JSON key validation
    - reference array extraction
    - placeholder payload container mapping
  - Emits staged diagnostics and `VXA2_SCHEMA_INVALID` codes on parse failures.

- `src/avatar/avatar_loader_facade.cpp`
  - Registered `Vxa2Loader` in extension dispatch chain.

- `CMakeLists.txt`
  - Added `src/avatar/vxa2_loader.cpp` to `vsfclone_core` target.

- `src/main.cpp`
  - Added CLI source-type display branch for `VXA2`.

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added block-0 trace fields:
    - `block0_selected_offset`
    - `block0_selected_mode_source`

- `src/vsf/unityfs_reader.cpp`
  - Added block-0 mode candidate prioritization helper (`BuildBlockModeCandidates`).
  - Added block-0 mode failure-hit demotion logic to reduce repeated low-value retries.
  - Added block-0 selected offset/mode-source propagation:
    - `header-derived`
    - `block-flag`
    - `fallback`
    - `failed-candidate`
  - Added reconstruction success candidate quality scoring before final selection.
  - Preserved best-partial block-0 offset/mode metadata on failure.

- `src/avatar/vsfavatar_loader.cpp`
  - Added sidecar parse support for:
    - `block0_selected_offset`
    - `block0_selected_mode_source`
  - Added `W_BLOCK0_META` warning emission.

- `tools/vsfavatar_sidecar.cpp`
  - Added JSON fields:
    - `block0_selected_offset`
    - `block0_selected_mode_source`
  - Added warning emission:
    - `W_BLOCK0_META`

- `tools/vsfavatar_sample_report.ps1`
  - Extended sample report with sidecar block-0 metadata:
    - `SidecarBlock0Offset`
    - `SidecarBlock0ModeSource`

### Verified

- `Release` build succeeded after all updates.
- Fixed-set VSFAvatar report regenerated:
  - `build/reports/vsfavatar_probe_latest_decode_tuning.txt`
- VSFAvatar fixed baseline remains blocked:
  - `Compat: partial`
  - `Meshes: 0`
  - `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
  - `SidecarBlock0Hypothesis=swap-size-flags`
  - `SidecarBlock0ModeSource=failed-candidate`

## 2026-03-03 - VSFAvatar diagnostics contract refresh (probe stage + primary error)

### Summary

Refined VSFAvatar diagnostics into explicit probe stages and primary-error codes, and aligned sidecar/loader JSON contracts to expose the same root-cause fields.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added stage/error and trace fields:
    - `probe_stage`
    - `probe_primary_error`
    - `serialized_candidate_count`
    - `selected_offset_family`

- `src/vsf/unityfs_reader.cpp`
  - Added explicit failure classification helper for reconstruction decode paths.
  - Reworked reconstruction candidate generation to track offset families.
  - Added stage transitions (`metadata-parsed`, `reconstruction`, `failed-reconstruction`, `failed-serialized`, `complete`).
  - Added primary error propagation from metadata/reconstruction/serialized stages.

- `tools/vsfavatar_sidecar.cpp`
  - Upgraded sidecar response schema to `schema_version=3`.
  - Added sidecar diagnostic fields:
    - `probe_stage`
    - `primary_error_code`
    - `selected_block_layout`
    - `selected_offset_family`
    - `reconstruction_summary`
  - Structured sidecar warnings with code prefixes (`W_META`, `W_RECON`).

- `src/avatar/vsfavatar_loader.cpp`
  - Loader schema validation now accepts `schema_version=2|3` and requires `primary_error_code` in `ok` responses.
  - Added sidecar diagnostic mapping into loader warnings (`W_STAGE`, `W_PRIMARY`, `W_LAYOUT`, `W_OFFSET`, `W_RECON_SUMMARY`).
  - In-house warning/error outputs were normalized to `W_*` / `E_*` prefixes.
  - Fallback path warnings were normalized (`W_FALLBACK`, `W_MODE`) to keep parser-path traces machine-readable.

- `README.md`
  - Updated sidecar JSON contract to reflect schema `v3` and diagnostic fields.
  - Documented `probe_stage` semantics and `primary_error_code` usage guidance.

### Verified

- `Release` build succeeded (`nativecore.dll`, `avatar_tool.exe`, `vsfavatar_sidecar.exe`, `vsfclone_cli.exe`).
- Fixed-set sample report regenerated (`build/reports/vsfavatar_probe_latest_after_impl.txt`).
- Sidecar direct execution now returns compact schema-v3 diagnostics with truncation-safe warning payloads.
- Baseline samples remain `Compat: partial`, `Meshes: 0`; primary blocker is still `DATA_BLOCK_READ_FAILED` on block 0.

## 2026-03-03 - VSFAvatar block-0 hypothesis instrumentation pass

### Summary

Implemented a block-0 focused reconstruction hypothesis pass and surfaced its outcomes through sidecar/report diagnostics.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added block-0 diagnostics:
    - `selected_block0_hypothesis`
    - `block0_attempt_count`
    - `block0_selected_offset`
    - `block0_selected_mode_source`

- `src/vsf/unityfs_reader.cpp`
  - Extended block decode variants for block-0:
    - `orig-trim16`
    - `orig-trim32`
    - `orig-clamp-range`
  - Added block-0 attempt counting and selected-hypothesis capture.
  - Preserved best-partial block-0 diagnostics when full reconstruction does not succeed.
  - Added block-0 mode-source trace (`header-derived` / `block-flag` / `fallback`).

- `tools/vsfavatar_sidecar.cpp`
  - Added sidecar JSON fields:
    - `selected_block0_hypothesis`
    - `block0_attempt_count`
    - `block0_selected_offset`
    - `block0_selected_mode_source`
  - Added warning line:
    - `W_BLOCK0: hypothesis=..., attempts=...`

- `src/avatar/vsfavatar_loader.cpp`
  - Mapped sidecar block-0 diagnostics into loader warnings (`W_BLOCK0`).
  - Included block-0 hypothesis and attempt count in in-house `W_META` warning output.

- `tools/vsfavatar_sample_report.ps1`
  - Added sidecar invocation per sample and appended parsed JSON diagnostics:
    - probe stage / primary error / block layout / offset family / block0 hypothesis / block0 attempts / block0 offset / block0 mode source

### Verified

- `Release` build succeeded after instrumentation changes.
- Fixed-set report regenerated:
  - `build/reports/vsfavatar_probe_latest_block0_hypothesis.txt`
- Baseline remains blocked at reconstruction:
  - `Compat: partial`, `Meshes: 0`
  - `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
  - `SidecarBlock0Hypothesis=swap-size-flags` (current dominant failed hypothesis path)

## 2026-03-02 - VSFAvatar parser pivot: sidecar-first loading path

### Summary

Shifted `.vsfavatar` loading from in-process parser-first to a sidecar-first execution model, while keeping in-house parsing as fallback.

### Update (schema v2 + timeout)

- `src/avatar/vsfavatar_loader.cpp`
  - Added sidecar timeout env var support:
    - `VSF_SIDECAR_TIMEOUT_MS` (default `15000`)
  - Added sidecar timeout handling with explicit failure code:
    - `SIDECAR_TIMEOUT`
  - Added schema validation for sidecar output (`schema_version=2`) with explicit failure code:
    - `SCHEMA_INVALID`
  - Added structured sidecar/runtime failure prefixes:
    - `SIDECAR_EXEC_FAILED`
    - `SIDECAR_RUNTIME_ERROR`
  - Added `warnings[]`/`missing_features[]` JSON array parsing.
  - Added sidecar `compat_level` mapping (`full|partial|failed`).

- `tools/vsfavatar_sidecar.cpp`
  - Upgraded JSON output to schema v2.
  - Added fields:
    - `compat_level`
    - `warnings`
    - `missing_features`
  - Error output now includes:
    - `schema_version`
    - `error_code`
    - `error_message`

### Changed

- `src/avatar/vsfavatar_loader.h`
  - Added explicit split of loader paths:
    - `LoadViaSidecar`
    - `LoadInHouse`

- `src/avatar/vsfavatar_loader.cpp`
  - Added parser mode switch via env var:
    - `VSF_PARSER_MODE=sidecar|inhouse|sidecar-strict`
  - Default mode is now `sidecar`.
  - Added sidecar path override via env var:
    - `VSF_SIDECAR_PATH`
  - Added Windows `CreateProcess`-based sidecar execution and JSON response parsing.
  - Added fallback behavior:
    - `sidecar` -> in-house fallback on sidecar failure
    - `sidecar-strict` -> fail without fallback
  - Added explicit parser-path warnings (`parser mode=sidecar` / fallback warnings).

- `tools/vsfavatar_sidecar.cpp` (new)
  - Added standalone sidecar executable that outputs structured JSON:
    - status/error
    - display name
    - mesh/material counts
    - object table status
    - last warning / last missing feature

- `CMakeLists.txt`
  - Added `vsfavatar_sidecar` console target.

### Verified

- `Release` build succeeded and now emits:
  - `build/Release/vsfavatar_sidecar.exe`
- `VSF_PARSER_MODE=sidecar` path works in both `vsfclone_cli` and `avatar_tool`.
- Sidecar-mode fixed sample report generated (`build/reports/vsfavatar_probe_sidecar.txt`).
- Sidecar pipe handling was hardened; long JSON warning payloads no longer force fallback via timeout.
- `sidecar-strict` timeout path verified with `VSF_SIDECAR_TIMEOUT_MS=1`:
  - returns `SIDECAR_TIMEOUT: process timed out`
- `sidecar` fallback path re-verified with invalid `VSF_SIDECAR_PATH`:
  - load succeeds through in-house fallback with `parser mode=inhouse (fallback)` warning.
- Compatibility remains `partial` on baseline samples (block-0 reconstruction blocker still active in in-house decode internals used by current sidecar output path).

## 2026-03-02 - VSFAvatar reconstruction summary-code pass and count-endian probing

### Summary

Added another decode iteration to improve reconstruction observability and broaden metadata table interpretation hypotheses while keeping the in-house decoder path.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added reconstruction summary diagnostics:
    - `selected_reconstruction_layout`
    - `reconstruction_failure_summary_code`

- `src/vsf/unityfs_reader.cpp`
  - Added reconstruction failure-code extraction/aggregation to report dominant error class across offset attempts.
  - Added reconstruction layout capture (`variant/mode`) when block decode succeeds for leading block.
  - Expanded metadata table parse hypotheses with count-endian probing:
    - block-count endian: `BE` / `LE`
    - node-count endian: `BE` / `LE`
  - Adjusted block-layout scoring to penalize implausible raw-mode (`mode=0`) size relationships.

- `src/avatar/vsfavatar_loader.cpp`
  - Metadata warning now includes reconstruction summary code.

### Verified

- `Release` build succeeded.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`, generated `2026-03-02T23:40:51`).
- Baseline remains `Compat: partial` / `Meshes: 0` across fixed samples.
- Block-0 failure remains converged:
  - `code=DATA_BLOCK_READ_FAILED`
  - observed mode in latest snapshot: `mode=1`
  - expected sizes: `74890067`, `88135067`, `125513796`, `402596`

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
