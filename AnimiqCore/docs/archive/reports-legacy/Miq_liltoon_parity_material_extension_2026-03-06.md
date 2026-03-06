# MIQ lilToon parity material extension (2026-03-06)

## Summary

This update expands the practical lilToon parity path for `.miq` by extending typed material extraction/import and native runtime shading coverage for high-impact visual controls.

Focus areas:

- richer lilToon typed parameter transport (`typed-v3`) from Unity exporter
- Unity importer parity for newly transported typed slots/properties
- native renderer consumption for emission texture and matcap path
- runtime-loader regression coverage for advanced lilToon typed entries

## Why this change

The prior path covered base/shade/normal/rim/emission-color phase-1 behavior, but still missed important lilToon controls that strongly affect final appearance:

- emission texture intensity linkage
- matcap color/texture contribution
- alias-heavy property naming across real avatars/material variants

Without these, Unity-to-native parity remained visibly inconsistent for many lilToon avatars.

## Changes

### 1) Unity extractor typed-v3 expansion

File:

- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`

Added lilToon typed extraction coverage:

- typed float:
  - `_EmissionStrength` (aliases: `_EmissionMapStrength`, `_EmissionStrength`)
  - `_MatCapBlend` (aliases: `_MatCapBlend`, `_MatCapBlendUV1`, `_MatCapStrength`)
- typed color:
  - `_MatCapColor` (aliases: `_MatCapColor`, `_MatCapTexColor`)
- typed texture slot:
  - `matcap` (aliases: `_MatCapTex`, `_MatCapTexture`, `_MatCapBlendMask`)

Feature-flag expansion:

- added `FeatureMatCap (1 << 6)` and enabled when matcap texture or color is present.

`shader_params_json` fallback capture also expanded for these additional float/color properties.

### 2) Unity importer typed apply parity

File:

- `unity/Packages/com.animiq.miq/Editor/MiqImporter.cs`

Importer now applies the new typed entries:

- texture slot `matcap` -> `_MatCapTex`, `_MatCapTexture`, `_MatCapBlendMask`
- color `_MatCapColor` -> `_MatCapColor`, `_MatCapTexColor`
- float `_EmissionStrength` -> `_EmissionMapStrength`, `_EmissionStrength`
- float `_MatCapBlend` -> `_MatCapBlend`, `_MatCapBlendUV1`, `_MatCapStrength`

This keeps Unity re-import behavior aligned with exported typed payload intent.

### 3) Native runtime lilToon shader/material expansion

File:

- `src/nativecore/native_core.cpp`

`GpuMaterialResource` extended with:

- `matcap_color`, `matcap_strength`
- `emission_srv`, `matcap_srv`

Pixel-shader pipeline expansion:

- constant-buffer fields:
  - `matcap_color`
  - `liltoon_aux`
- extra SRV bindings:
  - `t3`: emission texture
  - `t4`: matcap texture
- shader composition updates:
  - emission term multiplies emission texture when available
  - matcap term sampled in normal-based matcap UV space and applied with strength

Native material parse/apply expansion:

- typed/json float parse:
  - `_EmissionStrength`, `_EmissionMapStrength`
  - `_MatCapBlend`, `_MatCapBlendUV1`, `_MatCapStrength`
- typed/json color parse:
  - `_MatCapColor`, `_MatCapTexColor`
- typed texture resolution:
  - `emission` / `_EmissionMap`
  - `matcap` / `_MatCapTex` / `_MatCapTexture`

Diagnostic behavior:

- unresolved typed texture warnings now also cover `emission` and `matcap` slots.

### 4) Runtime loader regression test expansion

File:

- `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

Added advanced typed parse case:

- validates parsing of:
  - `_MatCapBlend`
  - `_EmissionStrength`
  - `_MatCapColor`
  - `matcap` texture slot

Also updated test payload builder options to generate advanced typed entries.

## Verification

Executed:

```powershell
cmake --build NativeAnimiq\build --config Release --target nativecore
```

Observed:

- nativecore build: PASS

Notes:

- Unity EditMode tests were updated but not executed in this shell-only environment.

## Compatibility notes

- Existing `typed-v2` parsing remains intact.
- Legacy `shader_params_json` fallback remains available.
- Added lilToon coverage is additive and backward-compatible.
