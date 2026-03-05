# XAV2 Standard/MToon strict parity expansion (2026-03-06)

## Summary

Expanded `.xav2` material parity contract from `liltoon/poiyomi` to `standard/mtoon/liltoon/poiyomi` across Unity and native loader paths, while preserving strict parity-fail behavior.

This update targets end-to-end consistency for:

- Unity export/extraction policy
- Unity runtime loader parity validation
- native XAV2 loader parity validation
- conversion manifest strict-shader policy
- format/documentation contract

## Why this change

Current strict parity contract rejects Standard/MToon families and hard-fails at runtime load. Real-world avatars frequently include these families, so `.xav2` compatibility was narrower than intended.

Goal:

- allow Standard/MToon as first-class families
- keep strict contract semantics (unsupported family still fails)
- keep typed-v3 normalization path consistent

## Changes

### 1) Native XAV2 loader parity family expansion + strict fail

File:

- `src/avatar/xav2_loader.cpp`

Changes:

- `IsSupportedShaderFamily(...)` extended with:
  - `standard`
  - `mtoon`
- `IsParityShaderFamily(...)` extended with:
  - `standard`
  - `mtoon`
- `InferShaderFamilyFromShaderName(...)` extended:
  - exact `Standard` -> `standard`
  - contains `mtoon` -> `mtoon`
- non-parity family handling keeps hard-fail behavior:
  - warning code: `XAV2_MATERIAL_SHADER_FAMILY_NOT_ALLOWED`
  - primary error: `XAV2_PARITY_CONTRACT_VIOLATION`

### 2) Unity runtime loader parity family expansion

File:

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`

Changes:

- `InferShaderFamilyFromShaderName(...)` extended for `standard/mtoon`.
- `IsParityShaderFamily(...)` expanded to `standard/mtoon/liltoon/poiyomi`.
- `IsSupportedShaderFamily(...)` expanded with `standard/mtoon`.
- strict parity failure semantics preserved:
  - unsupported family -> `ParityContractViolation`

### 3) Unity extractor/export typed-v3 policy widened

Files:

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2ExportOptions.cs`

Changes:

- extractor now emits `typed-v3` for:
  - `standard`
  - `mtoon`
  - `liltoon`
  - `poiyomi`
- baseline typed fields for all parity families:
  - typed color `_BaseColor`
  - typed float `_Cutoff`
  - typed texture slot `base`
- shader classification expansion:
  - `ResolveShaderVariant(...)`:
    - exact `Standard` -> `Standard`
    - contains `MToon` -> `MToon`
  - `ResolveShaderFamily(...)`:
    - `Standard` -> `standard`
    - `MToon` -> `mtoon`
- default strict shader allowlist expanded:
  - `Standard`
  - `MToon`
  - `lilToon`
  - `Poiyomi`

### 4) Conversion manifest strict shader policy update

File:

- `tools/vrm_to_xav2.cpp`

Changes:

- emitted `strictShaderSet` expanded with:
  - `Standard`
  - `MToon`

### 5) Format/runtime docs update

Files:

- `docs/formats/xav2.md`
- `unity/Packages/com.vsfclone.xav2/README.md`

Changes:

- typed section `shader_family` contract wording now includes:
  - `standard|mtoon|liltoon|poiyomi|legacy`
- package strict shader policy section updated with Standard/MToon entries.

### 6) Runtime test coverage additions

File:

- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

Added tests:

- `TryLoad_TypedMaterialStandardShaderFamily_DoesNotWarnUnsupported`
- `TryLoad_TypedMaterialMtoonShaderFamily_DoesNotWarnUnsupported`
- `TryLoad_TypedMaterialLegacyFamily_InfersStandardFromShaderName`

Also extended fixture builder to allow material shader-name override for inference validation.

## Compatibility notes

- Format section layout is unchanged (`0x0015` wire shape unchanged).
- This is policy/normalization expansion, not a binary schema bump.
- Unsupported families outside parity set still fail by contract.

## Verification

Verified by source-level consistency checks:

- parity/supported/inference functions updated in both Unity runtime and native loader
- extractor/export strict policy and typed-v3 defaults aligned
- docs and tests updated to reflect new contract

Not executed in this shell session:

- Unity test runner
- full native build/regression gate scripts

