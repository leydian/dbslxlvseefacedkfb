# VXA2 TLV Decode Detailed Update Report (2026-03-02)

## Scope

This report captures the detailed implementation and validation outcome for the VXA2 TLV decode MVP pass that moved `.vxa2` loading from `header+manifest` only to `section-aware payload mapping`.

## Implemented Changes

1. Runtime loader (`src/avatar/vxa2_loader.cpp`)
- Added section-table traversal immediately after `manifest_json`.
- Implemented TLV section header parse:
  - `type (u16)`
  - `flags (u16)`
  - `size (u32)`
- Implemented known section decoders:
  - `0x0001` mesh blob section
  - `0x0002` texture blob section
  - `0x0003` material override section
- Added strict parse safety rules:
  - invalid/truncated section header or payload boundary -> `VXA2_SECTION_TRUNCATED`
  - malformed known section payload -> `VXA2_SCHEMA_INVALID`
- Added manifest-reference coverage classification:
  - unresolved mesh/texture refs -> `VXA2_ASSET_MISSING`
- Kept compatibility policy:
  - parse success + incomplete coverage => `Compat: partial`
  - full reference coverage => `Compat: full`

2. Package/API diagnostics
- `include/vsfclone/avatar/avatar_package.h`
  - `format_section_count`
  - `format_decoded_section_count`
  - `format_unknown_section_count`
- `include/vsfclone/nativecore/api.h` (`NcAvatarInfo`)
  - same 3 counters surfaced through C ABI
- `src/nativecore/native_core.cpp`
  - mapped package counters into `NcAvatarInfo`
- `tools/avatar_tool.cpp`
  - added output lines:
    - `FormatSections`
    - `FormatDecodedSections`
    - `FormatUnknownSections`

3. Documentation sync
- `docs/formats/vxa2.md`
  - promoted TLV section layout to concrete v1 contract
  - documented known section type payload layouts
  - documented unknown-type skip behavior
- `README.md`
  - updated VXA2 capability line to section decode MVP
  - appended latest behavior notes for TLV decode diagnostics

## Verification Summary

Build verification:
- `cmake --build build_vxa2 --config Release` succeeded.

Runtime verification (`build_vxa2/Release/avatar_tool.exe`):

1. Baseline sample (`sample/demo_mvp.vxa2`)
- `Format: VXA2`
- `Compat: partial`
- `ParserStage: runtime-ready`
- `PrimaryError: VXA2_ASSET_MISSING`
- `FormatSections: 0`
- `FormatDecodedSections: 0`

2. TLV-populated sample (`build/tmp_vx/demo_tlv.vxa2`)
- `Format: VXA2`
- `Compat: full`
- `ParserStage: runtime-ready`
- `PrimaryError: NONE`
- `FormatSections: 3`
- `FormatDecodedSections: 3`
- `FormatUnknownSections: 0`

3. Truncated TLV sample (`build/tmp_vx/demo_tlv_truncated.vxa2`)
- `Format: VXA2`
- `Compat: failed`
- `ParserStage: resolve`
- `PrimaryError: VXA2_SECTION_TRUNCATED`

Regression check:
- `sample/demo_mvp.vxavatar` remained `Compat: full`, `ParserStage: runtime-ready`.

## Known Limitations

- Section payload schema is MVP-level and intentionally narrow (`0x0001/0x0002/0x0003`).
- Unknown section types are skipped, but semantic decode is deferred.
- No converter (`.vxavatar -> .vxa2`) is included in this pass.

## Next Steps

1. Add unit/integration tests for malformed section payloads and unknown section combinations.
2. Define material override schema v2 (typed fields instead of fixed 3-string tuple).
3. Implement converter utility and emit conversion coverage report.
