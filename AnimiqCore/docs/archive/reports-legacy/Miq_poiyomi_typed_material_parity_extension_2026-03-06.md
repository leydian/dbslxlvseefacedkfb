# MIQ Poiyomi typed material parity extension (2026-03-06)

## Summary

This update expands Poiyomi material extraction into the same advanced typed-v3 parameter family already used for lilToon-oriented parity work, so cross-family material reconstruction can consume a more consistent typed contract.

## Why this change

The previous Poiyomi path emitted only a minimal typed baseline (`_BaseColor`, `_Cutoff`, `base` texture). That limited downstream visual parity because native/runtime material reconstruction already supports richer typed entries.

## Changes

### 1) Unity extractor: Poiyomi advanced typed-v3 emission

File:

- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`

Added typed extraction coverage for `shader_family=poiyomi`:

- typed colors:
  - `_ShadeColor`
  - `_EmissionColor`
  - `_RimColor`
  - `_MatCapColor` (aliases: `_MatCapColor`, `_MatCapTexColor`)
- typed floats:
  - `_BumpScale`
  - `_RimFresnelPower`
  - `_RimLightingMix`
  - `_EmissionStrength` (aliases: `_EmissionMapStrength`, `_EmissionStrength`)
  - `_MatCapBlend` (aliases: `_MatCapBlend`, `_MatCapBlendUV1`, `_MatCapStrength`)
- typed texture slots:
  - `shade`
  - `normal`
  - `emission`
  - `rim`
  - `matcap`

Feature flags are now emitted for Poiyomi when signals exist:

- `FeatureShade`
- `FeatureNormalMap`
- `FeatureEmission`
- `FeatureRim`
- `FeatureMatCap`

### 2) Runtime test coverage

File:

- `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

Added:

- `TryLoad_TypedMaterialParams_AdvancedPoiyomiEntries_Parses`

Test validates:

- Poiyomi family is accepted in typed material parse path.
- advanced typed entries parse as expected:
  - float: `_MatCapBlend`, `_EmissionStrength`
  - color: `_MatCapColor`
  - texture slot: `matcap`
- no unsupported shader-family warning code is emitted.

## Compatibility notes

- No format section change (`0x0015`) was introduced.
- This is additive extraction/test coverage; existing typed-v2/v3 read paths remain unchanged.
- Existing fallback behavior for missing optional typed entries is preserved.

## Verification

Confirmed by source-level regression additions:

- extractor Poiyomi typed emission expanded in `MiqAvatarExtractors.cs`
- runtime loader test added in `MiqRuntimeLoaderTests.cs`

Unity test runner execution was not performed in this shell-only environment.

