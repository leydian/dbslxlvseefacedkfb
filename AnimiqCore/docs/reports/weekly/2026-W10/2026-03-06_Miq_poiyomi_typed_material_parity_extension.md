# MIQ Poiyomi typed material parity extension (2026-03-06)

## Summary

Poiyomi extraction now emits advanced typed-v3 material entries aligned with the current lilToon parity transport set, improving cross-family shading reconstruction consistency.

## Scope

- Unity extractor typed-v3 emission (`poiyomi`)
- Unity runtime loader test coverage
- documentation/index updates

## Implementation details

### 1) Poiyomi typed extraction expansion

Updated:

- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`

Previously Poiyomi emitted minimal typed baseline only:

- `_BaseColor`
- `_Cutoff`
- `slot=base`

Now Poiyomi additionally emits:

- colors:
  - `_ShadeColor`
  - `_EmissionColor`
  - `_RimColor`
  - `_MatCapColor`
- floats:
  - `_BumpScale`
  - `_RimFresnelPower`
  - `_RimLightingMix`
  - `_EmissionStrength`
  - `_MatCapBlend`
- textures:
  - `shade`
  - `normal`
  - `emission`
  - `rim`
  - `matcap`

Feature-flag derivation expanded:

- `FeatureShade`
- `FeatureNormalMap`
- `FeatureEmission`
- `FeatureRim`
- `FeatureMatCap`

### 2) Runtime parse regression coverage

Updated:

- `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

Added test:

- `TryLoad_TypedMaterialParams_AdvancedPoiyomiEntries_Parses`

Assertions:

- typed Poiyomi payload loads successfully.
- `_MatCapBlend`, `_EmissionStrength`, `_MatCapColor`, `slot=matcap` are parsed.
- `MIQ_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY` is not emitted.

## Compatibility

- no schema/wire-format change (`typed-v3`/`0x0015` unchanged)
- additive behavior only; existing typed-v2/legacy compatibility preserved

## Verification

- source-level validation complete (extractor + runtime test updates)
- Unity test execution not run in current shell-only environment

