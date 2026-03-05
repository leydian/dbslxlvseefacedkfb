# XAV2 Standard/MToon strict parity expansion (2026-03-06)

## Summary

Expanded `.xav2` strict parity family contract to include `standard` and `mtoon` in addition to existing `liltoon` and `poiyomi`.

## Key updates

- Native XAV2 loader:
  - parity/supported family checks include `standard/mtoon`
  - shader-name inference supports `Standard` and `MToon`
  - unsupported families still fail with parity-contract error
- Unity runtime loader:
  - parity/supported family checks include `standard/mtoon`
  - shader-name inference supports `Standard` and `MToon`
  - strict parity failure semantics preserved
- Unity extractor/export:
  - `typed-v3` emission expanded to `standard/mtoon` families
  - baseline typed fields (`_BaseColor`, `_Cutoff`, `base`) emitted for all parity families
  - default strict shader set expanded with `Standard` and `MToon`
- Conversion/format docs:
  - `vrm_to_xav2` manifest `strictShaderSet` updated
  - `docs/formats/xav2.md` shader-family wording updated
  - package README strict policy list updated
- Runtime tests:
  - new Standard/MToon acceptance + Standard inference cases added

## Files changed

- `src/avatar/xav2_loader.cpp`
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2ExportOptions.cs`
- `tools/vrm_to_xav2.cpp`
- `docs/formats/xav2.md`
- `unity/Packages/com.vsfclone.xav2/README.md`
- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

## Verification

- Source-level parity/policy alignment checks completed.
- Unity runner and full gate scripts were not executed in this shell session.

