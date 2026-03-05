# XAV2 SDK Bootstrap Report (2026-03-02)

## Scope

This update introduces the first end-to-end XAV2 path in this repository:

- runtime loader support for `.xav2`
- native API format hint exposure
- VRM to XAV2 converter CLI
- Unity package scaffold for export/runtime parsing
- format documentation and project doc index updates

## Implemented Changes

1. Runtime loader path
- Added:
  - `src/avatar/xav2_loader.h`
  - `src/avatar/xav2_loader.cpp`
- Loader contract:
  - extension: `.xav2`
  - header: `XAV2`, `version=1`
  - required manifest keys:
    - `avatarId`
    - `meshRefs`
    - `materialRefs`
    - `textureRefs`
- TLV sections:
  - `0x0011` mesh render payload
  - `0x0002` texture blob
  - `0x0003` material override
  - `0x0012` material shader params JSON
- Error/compat behavior:
  - `XAV2_SCHEMA_INVALID`
  - `XAV2_SECTION_TRUNCATED`
  - `XAV2_ASSET_MISSING`
  - partial compatibility on reference/payload mismatch

2. Loader facade registration
- Updated `src/avatar/avatar_loader_facade.cpp`
  - `.xav2` route added in the dispatcher chain.

3. Avatar/native API model extension
- Updated `include/vsfclone/avatar/avatar_package.h`
  - added `AvatarSourceType::Xav2`
  - added `MaterialRenderPayload.shader_params_json`
- Updated `include/vsfclone/nativecore/api.h`
  - added `NC_AVATAR_FORMAT_XAV2 = 5`
- Updated `src/nativecore/native_core.cpp`
  - mapped `AvatarSourceType::Xav2` to `NC_AVATAR_FORMAT_XAV2`
- Updated `host/HostCore/NativeCoreInterop.cs`
  - added managed enum mapping `NcAvatarFormatHint.Xav2`

4. VRM to XAV2 converter
- Added `tools/vrm_to_xav2.cpp`
- Added target in `CMakeLists.txt`:
  - `vrm_to_xav2`
- Converter behavior:
  - input: `.vrm`
  - output: `.xav2`
  - source: runtime-extracted payloads from `AvatarLoaderFacade`
  - writes mesh/material/texture sections and shader params section

5. Unity package scaffold
- Added package root:
  - `unity/Packages/com.vsfclone.xav2/package.json`
- Added runtime model/parser scaffold:
  - `Runtime/Xav2DataModel.cs`
  - `Runtime/Xav2RuntimeLoader.cs`
  - `Runtime/VsfClone.Xav2.Runtime.asmdef`
- Added editor exporter scaffold:
  - `Editor/Xav2Exporter.cs`
  - `Editor/Xav2ExportOptions.cs`
  - `Editor/VsfClone.Xav2.Editor.asmdef`
- Added package readme:
  - strict shader set policy baseline:
    - `lilToon`, `Poiyomi`, `potatoon`, `realtoon`

6. Documentation updates
- Added format spec:
  - `docs/formats/xav2.md`
- Updated docs index and README references to include XAV2 tooling/path.

## Build/Validation

Validated with:

- `cmake -S NativeVsfClone -B NativeVsfClone/build -G "Visual Studio 17 2022" -A x64`
- `cmake --build NativeVsfClone/build --config Release --target vsfclone_core nativecore vrm_to_xav2`

Validation result:

- `vsfclone_core` build success
- `nativecore.dll` build success
- `vrm_to_xav2.exe` build success

## Known Gaps (Intentional)

- Unity exporter currently assumes payload input model and does not yet include direct VRM scene decode pipeline.
- Native renderer currently consumes generalized material payloads; shader-specific full visual parity is not yet implemented.
- Image-comparison quality gate automation for XAV2 is not added in this change.

## Next Steps

1. Wire Unity-side VRM ingest path to produce XAV2 payloads directly from project assets.
2. Add shader-specific parameter schema/versioning policy for lilToon/Poiyomi/potatoon/realtoon.
3. Add XAV2 quality gate scripts (fixed set + image-compare threshold).
