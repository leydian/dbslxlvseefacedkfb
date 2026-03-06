# MIQ SDK Bootstrap Report (2026-03-02)

## Scope

This update introduces the first end-to-end MIQ path in this repository:

- runtime loader support for `.miq`
- native API format hint exposure
- VRM to MIQ converter CLI
- Unity package scaffold for export/runtime parsing
- format documentation and project doc index updates

## Implemented Changes

1. Runtime loader path
- Added:
  - `src/avatar/miq_loader.h`
  - `src/avatar/miq_loader.cpp`
- Loader contract:
  - extension: `.miq`
  - header: `MIQ`, `version=1`
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
  - `MIQ_SCHEMA_INVALID`
  - `MIQ_SECTION_TRUNCATED`
  - `MIQ_ASSET_MISSING`
  - partial compatibility on reference/payload mismatch

2. Loader facade registration
- Updated `src/avatar/avatar_loader_facade.cpp`
  - `.miq` route added in the dispatcher chain.

3. Avatar/native API model extension
- Updated `include/animiq/avatar/avatar_package.h`
  - added `AvatarSourceType::Miq`
  - added `MaterialRenderPayload.shader_params_json`
- Updated `include/animiq/nativecore/api.h`
  - added `NC_AVATAR_FORMAT_MIQ = 5`
- Updated `src/nativecore/native_core.cpp`
  - mapped `AvatarSourceType::Miq` to `NC_AVATAR_FORMAT_MIQ`
- Updated `host/HostCore/NativeCoreInterop.cs`
  - added managed enum mapping `NcAvatarFormatHint.Miq`

4. VRM to MIQ converter
- Added `tools/vrm_to_miq.cpp`
- Added target in `CMakeLists.txt`:
  - `vrm_to_miq`
- Converter behavior:
  - input: `.vrm`
  - output: `.miq`
  - source: runtime-extracted payloads from `AvatarLoaderFacade`
  - writes mesh/material/texture sections and shader params section

5. Unity package scaffold
- Added package root:
  - `unity/Packages/com.animiq.miq/package.json`
- Added runtime model/parser scaffold:
  - `Runtime/MiqDataModel.cs`
  - `Runtime/MiqRuntimeLoader.cs`
  - `Runtime/Animiq.Miq.Runtime.asmdef`
- Added editor exporter scaffold:
  - `Editor/MiqExporter.cs`
  - `Editor/MiqExportOptions.cs`
  - `Editor/Animiq.Miq.Editor.asmdef`
- Added package readme:
  - strict shader set policy baseline:
    - `lilToon`, `Poiyomi`, `potatoon`, `realtoon`

6. Documentation updates
- Added format spec:
  - `docs/formats/miq.md`
- Updated docs index and README references to include MIQ tooling/path.

## Build/Validation

Validated with:

- `cmake -S NativeAnimiq -B NativeAnimiq/build -G "Visual Studio 17 2022" -A x64`
- `cmake --build NativeAnimiq/build --config Release --target animiq_core nativecore vrm_to_miq`

Validation result:

- `animiq_core` build success
- `nativecore.dll` build success
- `vrm_to_miq.exe` build success

## Known Gaps (Intentional)

- Unity exporter currently assumes payload input model and does not yet include direct VRM scene decode pipeline.
- Native renderer currently consumes generalized material payloads; shader-specific full visual parity is not yet implemented.
- Image-comparison quality gate automation for MIQ is not added in this change.

## Next Steps

1. Wire Unity-side VRM ingest path to produce MIQ payloads directly from project assets.
2. Add shader-specific parameter schema/versioning policy for lilToon/Poiyomi/potatoon/realtoon.
3. Add MIQ quality gate scripts (fixed set + image-compare threshold).
