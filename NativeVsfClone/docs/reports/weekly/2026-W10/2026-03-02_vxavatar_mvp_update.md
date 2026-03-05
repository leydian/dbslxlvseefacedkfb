# VXAvatar MVP Update Report (2026-03-02)

## Scope

This report summarizes the implementation and verification work completed for `.vxavatar` MVP runtime loading and the `.vxa2` onboarding artifacts that were requested for parallel migration planning.

## Implemented in This Update

1. `.vxavatar` parser hardening in runtime loader
   - Added ZIP compression method branching for payload reads.
   - Added deflate (`method=8`) compatibility using a temporary PowerShell/.NET extraction path.
   - Preserved existing stored-entry (`method=0`) in-house payload path.
   - Added UTF-8 BOM stripping for `manifest.json` before JSON parse.

2. `.vxavatar` diagnostics and behavior
   - Preserved stage model: `parse -> resolve -> payload -> runtime-ready`.
   - Preserved/used error code model:
     - `VX_MANIFEST_MISSING`
     - `VX_SCHEMA_INVALID`
     - `VX_ASSET_MISSING`
     - `VX_UNSUPPORTED_COMPRESSION`
   - Added external extractor warning when deflate fallback is used.

3. `.vxa2` migration artifact
   - Added draft format spec document:
     - `docs/formats/vxa2.md`
   - Document includes:
     - magic/version/manifest layout
     - required keys
     - validation and compatibility rules
     - current runtime support status

## Verification Summary

Build:

- `cmake --build build --config Release` completed successfully.

Runtime check (`avatar_tool`):

- Input: `D:\dbslxlvseefacedkfb\sample\demo_mvp.vxavatar`
- Result:
  - `Format: VXAvatar`
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`
  - `MeshPayloads: 1`
  - `MaterialPayloads: 1`
  - `TexturePayloads: 1`

## Known Limitations

- Deflate extraction currently relies on external PowerShell/.NET invocation.
- The current fallback is functional for MVP but should be replaced with in-process decompression for production stability/performance.

## Recommended Next Steps

1. Replace deflate fallback with in-process decompressor.
2. Add dedicated unit/integration tests for:
   - stored vs deflate ZIP entries
   - BOM/no-BOM manifests
   - missing/invalid manifest keys
3. Add `.vxavatar -> .vxa2` converter utility and conversion report output.
