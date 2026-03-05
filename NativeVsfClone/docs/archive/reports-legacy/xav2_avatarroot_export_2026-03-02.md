# XAV2 AvatarRoot Export Update (2026-03-02)

## Summary

Implemented Unity-side direct export path from Scene `AvatarRoot` to `.xav2` with strict shader validation and extended payload sections for skinning and blendshapes.

## Implemented Changes

1. Unity exporter path
- `Editor/Xav2Exporter.cs`
  - Added overload:
    - `Export(string outputPath, GameObject avatarRoot, Xav2ExportOptions options)`
  - Keeps payload-based export path and now writes additional sections:
    - `0x0013` skin payload
    - `0x0014` blendshape payload

2. AvatarRoot extractor
- `Editor/Xav2AvatarExtractors.cs` (new)
  - Added extractor contract:
    - `IXav2AvatarExtractor`
  - Added `UniVrmAvatarExtractor` implementation for scene-imported avatars:
    - mesh/material/texture extraction
    - skin payload extraction (`boneIndices`, `bindPoses`, weight blob)
    - blendshape payload extraction (frame deltas)
    - strict shader policy compatibility path

3. Unity editor entry
- `Editor/Xav2ExportMenu.cs` (new)
  - Added menu:
    - `Tools/VsfClone/XAV2/Export Selected AvatarRoot`

4. Runtime model/loader extension
- `Runtime/Xav2DataModel.cs`
  - Added manifest metadata:
    - `schemaVersion`, `exporterVersion`, `hasSkinning`, `hasBlendShapes`
  - Added skin/blendshape payload model types.
  - Added `shaderVariant` to material payload.

- `Runtime/Xav2RuntimeLoader.cs`
  - Added parsing for section types `0x0013`, `0x0014`
  - Added material payload backward-compatible parsing with/without `shaderVariant`

5. Native parser extension
- `include/vsfclone/avatar/avatar_package.h`
  - Added `SkinRenderPayload`, `BlendShapeRenderPayload`, and material `shader_variant`
- `src/avatar/xav2_loader.cpp`
  - Added parsing for `0x0013`, `0x0014`
  - Added error codes:
    - `XAV2_SKIN_SCHEMA_INVALID`
    - `XAV2_BLENDSHAPE_SCHEMA_INVALID`
  - Added backward-compatible material section decode path

6. Converter and spec sync
- `tools/vrm_to_xav2.cpp`
  - Manifest metadata fields added (`schemaVersion`, `exporterVersion`, skin/blend flags)
  - Material section now writes `shader_variant`

- `docs/formats/xav2.md`
  - Added manifest metadata keys and new section contracts (`0x0013`, `0x0014`)

## Validation

Native build validated:

- `cmake --build NativeVsfClone/build --config Release --target vsfclone_core nativecore vrm_to_xav2`

Result:

- `vsfclone_core` success
- `nativecore` success
- `vrm_to_xav2` success

## Remaining Gaps

- UniVRM package-specific typed integration hooks are not yet explicitly referenced; current path uses scene AvatarRoot component extraction.
- Visual parity gate (image-compare threshold) is not included in this update.
- 30-sample fixed-set automation is not yet wired.
