# XAV2 Physics Typed Sections and Pipeline Rollout (2026-03-06)

## Scope

This report covers the first implementation slice for transporting VRM/VRChat physics metadata through `.xav2` using typed sections and keeping Unity/native loader behavior aligned.

In scope:

- XAV2 format and manifest extension for physics metadata
- Unity exporter/extractor/importer/runtime loader support
- Native XAV2 loader contract and parser updates
- Diagnostics and test coverage updates

Out of scope:

- Native runtime secondary-motion simulation (solver execution)
- Full one-to-one semantic parity with all SDK-specific advanced physics options

## Implemented Changes

### 1) Format and manifest expansion

- Added manifest fields:
  - `physicsSchemaVersion`
  - `physicsSource` (`none|vrm|vrc|mixed`)
  - `hasSpringBones`
  - `hasPhysBones`
- Added typed section layouts:
  - `0x0018`: SpringBone typed payload
  - `0x0019`: PhysBone typed payload
  - `0x001A`: Physics collider typed payload
- Updated doc:
  - `docs/formats/xav2.md`

### 2) Unity runtime data + loader support

- Added payload models:
  - `Xav2PhysicsColliderPayload`
  - `Xav2SpringBonePayload`
  - `Xav2PhysBonePayload`
- Extended `Xav2Manifest` and `Xav2AvatarPayload` with physics fields/collections.
- Added parser path for `0x0018/0x0019/0x001A`.
- Added physics validation/warning contracts:
  - `XAV2_PHYSICS_SCHEMA_INVALID`
  - `XAV2_PHYSICS_REF_MISSING`
  - `XAV2_PHYSICS_COMPONENT_UNAVAILABLE`
- File:
  - `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
  - `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`

### 3) Unity export/import extraction path

- Exporter:
  - Writes physics typed sections (`0x0018/0x0019/0x001A`).
  - Emits manifest physics flags/source with deterministic defaults.
- Extractor:
  - Added reflection-based metadata extraction for PhysBone/SpringBone/collider-like components.
  - Keeps SDK dependency optional (no hard compile-time package dependency).
- Importer:
  - Added reflection-based best-effort component rehydration path.
  - Missing component types result in warning-only behavior (import continues).
- Files:
  - `unity/Packages/com.vsfclone.xav2/Editor/Xav2Exporter.cs`
  - `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
  - `unity/Packages/com.vsfclone.xav2/Editor/Xav2Importer.cs`

### 4) Native loader and package contract alignment

- Added native payload structs:
  - `PhysicsColliderPayload`
  - `SpringBonePayload`
  - `PhysBonePayload`
- Added XAV2 parser support for physics sections.
- Added payload-level collider reference checks and warnings.
- Runtime limitation is explicit and non-fatal:
  - `XAV2_PHYSICS_COMPONENT_UNAVAILABLE: runtime_simulation_not_implemented`
- Files:
  - `include/vsfclone/avatar/avatar_package.h`
  - `src/avatar/xav2_loader.cpp`

### 5) Test coverage updates

- Runtime tests:
  - physics section parse success
  - missing collider reference warning path
- Editor tests:
  - exporter round-trip persistence for physics payloads
- Files:
  - `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`
  - `unity/Packages/com.vsfclone.xav2/Tests/Editor/Xav2ExporterTests.cs`

## Verification Summary

- Executed commands:
  - `cmake --build NativeVsfClone\build --config Release --target nativecore avatar_tool`
  - `NativeVsfClone\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.xav2"`
- Outcomes:
  - Native build: PASS
  - Sample `.xav2` load via `avatar_tool`: PASS
    - `Compat=full`
    - `ParserStage=runtime-ready`
    - `PrimaryError=NONE`
- Not executed in this shell:
  - Unity Editor test runner (EditMode) for updated C# tests

## Known Risks or Limitations

- Native runtime solver is still not implemented for SpringBone/PhysBone simulation; current behavior is metadata transport + warning contract.
- Reflection-based extractor/importer paths are resilient to missing SDKs but can under-map provider-specific advanced options when field names diverge.
- Importer rehydration is best-effort and intentionally non-fatal under missing component assemblies.

## Next Steps

1. Implement native secondary-motion solver execution path over parsed payloads (`SpringBoneSolver`/`PhysBoneSolver`).
2. Add deterministic parity fixtures (VRM-only, VRC-only, mixed) and gate-level assertions for physics payload integrity.
3. Expand importer rehydration mappings to cover more provider-specific property aliases and collider variants.
