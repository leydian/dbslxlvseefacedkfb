# XAV2 lilToon Texture Export Fallback Update (2026-03-05)

## Summary

This update addresses a post-export rendering symptom where `.xav2` avatars exported from Unity (including lilToon-based avatars) appeared as flat silhouette-like output with missing surface texture detail.

Observed behavior:

- Export succeeded and `.xav2` file loaded.
- Runtime draw calls were present.
- Visual output showed a near-solid body color instead of expected albedo textures.

Root cause identified in Unity exporter:

- texture extraction relied on `Texture2D.EncodeToPNG()` only.
- when source texture is non-readable/compressed/import-restricted, `EncodeToPNG()` can fail.
- exporter previously swallowed the exception and wrote an empty texture payload.
- runtime then had no valid texture SRV to sample and rendered fallback base tint.

## Changes Implemented

Updated file:

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`

### 1) Robust texture encode fallback path

- `EncodeTextureSafe(Texture2D)` now:
  - first tries `texture.EncodeToPNG()`
  - on failure, falls back to `EncodeTextureViaRenderTexture(...)`

Fallback strategy:

- blit source texture into temporary `RenderTexture`
- read back pixels via `ReadPixels` into temporary readable `Texture2D`
- encode readable texture to PNG bytes
- clean up temporary resources (`RenderTexture`, temp texture)

Result:

- non-readable source textures can still be serialized into `.xav2` texture payloads.

### 2) Base-color texture property resolution expanded

- Added `ResolveBaseColorTexture(Material)` to probe properties in order:
  - `_MainTex`
  - `_BaseMap`
  - `_BaseColorMap`

Why:

- lilToon/material variants do not always expose base map only through `_MainTex`.
- broader probe improves texture capture reliability across shader variants.

### 3) Guarded texture reference population

- `BaseColorTextureName` is now only linked when encoded bytes are non-empty.
- if encoding fails even after fallback, exporter logs warning and continues.

Added warning:

- `[XAV2] Texture encode failed for material '{material}', texture '{texture}'.`

Operational impact:

- makes silent texture-drop scenarios visible in Unity console.

## Runtime/Compatibility Impact

No runtime API or format contract change:

- no changes to native loader section schema
- no changes to host/native core interfaces
- no changes to render pipeline contract

Behavioral impact is exporter-side robustness only.

## Verification Notes

Static/code-path verification completed:

- confirmed new fallback encode path and cleanup flow
- confirmed expanded property probe logic
- confirmed warning emission on hard failure

Manual Unity verification recommended:

1. Export a lilToon avatar that previously rendered as flat silhouette.
2. Load exported `.xav2` in host.
3. Confirm texture detail appears and warning is absent.
4. If warning appears, identify texture import/source constraints from material inspector and Unity console.

## Files in This Update

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
- `docs/reports/xav2_liltoon_texture_export_fallback_2026-03-05.md`
- `docs/reports/session_change_summary_2026-03-05.md`
- `docs/INDEX.md`
