# MIQ typed-v2 material params (lilToon phase1) implementation report (2026-03-06)

## Summary

This update introduces a structured material parameter path for MIQ v2 (`0x0015`) and wires it across:

- Unity exporter
- Unity runtime loader
- native MIQ loader
- native renderer consumption

The focus is lilToon-first (`shader_family=liltoon`) while preserving legacy behavior through fallback to `shader_params_json`.

## Why this change

Previous path relied heavily on string parsing from `shader_params_json`, which made robust shader reconstruction difficult and fragile.

Typed payloads reduce ambiguity and provide a stable extension point for upcoming higher-fidelity lilToon rendering.

## Implementation details

### 1) Data model expansion

Files:

- `include/animiq/avatar/avatar_package.h`
- `unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`

Added fields:

- material metadata:
  - `shader_family`
  - `material_param_encoding`
  - `feature_flags` (`uint32`)
- typed parameter lists:
  - float params (`id`, `value`)
  - color params (`id`, `rgba`)
  - texture params (`slot`, `texture_ref`)

### 2) New section type `0x0015` (MaterialTypedParams)

Files:

- `src/avatar/miq_loader.cpp`
- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
- `docs/formats/miq.md`

Payload parse order:

1. `material_name`
2. `shader_family`
3. `feature_flags`
4. float list
5. color list
6. texture list

Validation/diagnostics:

- schema parse failure: `MIQ_MATERIAL_TYPED_SCHEMA_INVALID`
- unsupported shader family warning: `MIQ_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY`
- liltoon required param warning (`_BaseColor`): `MIQ_MATERIAL_TYPED_MISSING_REQUIRED_PARAM`

### 3) Unity exporter/extractor typed emission

Files:

- `unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`

Exporter behavior:

- always writes legacy `0x0003` + `0x0012` for compatibility
- writes `0x0015` when typed params are present
- sets manifest `materialParamEncoding` to `typed-v2` when typed data exists

Extractor behavior (lilToon phase1):

- typed float params:
  - `_Cutoff`
  - `_BumpScale`
  - `_RimFresnelPower`
  - `_RimLightingMix`
- typed color params:
  - `_BaseColor`
  - `_ShadeColor`
  - `_EmissionColor`
  - `_RimColor`
- typed texture slots:
  - `base`, `shade`, `normal`, `emission`, `mask`, `rim`
- feature flag derivation:
  - cutout/transparent from alpha mode
  - shade/normal/emission/rim from extracted data presence

### 4) Native renderer typed-priority consumption

File:

- `src/nativecore/native_core.cpp`

Rendering/material binding now prefers typed params for:

- alpha mode flags (`feature_flags` cutout/transparent)
- alpha cutoff (`_Cutoff`)
- base/shade/emission color (`_BaseColor`, `_ShadeColor`, `_EmissionColor`)
- base texture reference (`slot=base`)

Fallback policy:

- if typed value is missing, existing `shader_params_json` parsing path is used.

## Test and verification

### Native build/runtime checks

Commands:

```powershell
cmake --build NativeAnimiq\build --config Release --target nativecore avatar_tool
NativeAnimiq\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.miq"
```

Results:

- build: PASS
  - one transient linker lock (`LNK1104` on `avatar_tool.exe`) due active process; resolved by stopping process and rebuilding.
- sample parse: PASS
  - `Load succeeded`
  - `Format=MIQ`
  - `Compat=full`
  - `ParserStage=runtime-ready`
  - `PrimaryError=NONE`

### Unity runtime test update

File:

- `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

Updates:

- fixed unsupported-version test (uses version `3` now that `2` is valid)
- added typed section parse test verifying:
  - `MaterialParamEncoding == typed-v2`
  - `ShaderFamily == liltoon`
  - float/color/texture typed lists are populated

## Current limits and follow-up

- This patch does not implement full lilToon parity yet (normal-map shading logic, rim lighting model, advanced keyword matrix, multi-pass behavior).
- Typed-v2 schema is currently lilToon-first and intentionally conservative.
- Next step should build on this schema to complete high-fidelity lilToon rendering and add image regression gates.
