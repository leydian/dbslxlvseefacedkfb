# MIQ Standard/MToon strict parity expansion (2026-03-06)

## Summary

Expanded `.miq` strict parity family contract to include `standard` and `mtoon` in addition to existing `liltoon` and `poiyomi`.

## Key updates

- Native MIQ loader:
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
  - `vrm_to_miq` manifest `strictShaderSet` updated
  - `docs/formats/miq.md` shader-family wording updated
  - package README strict policy list updated
- Runtime tests:
  - new Standard/MToon acceptance + Standard inference cases added

## Files changed

- `src/avatar/miq_loader.cpp`
- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqExportOptions.cs`
- `tools/vrm_to_miq.cpp`
- `docs/formats/miq.md`
- `unity/Packages/com.animiq.miq/README.md`
- `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

## Verification

- Source-level parity/policy alignment checks completed.
- Unity runner and full gate scripts were not executed in this shell session.

