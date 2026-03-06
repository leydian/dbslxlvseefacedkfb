# MIQ Unity SDK Diagnostics + Gate Expansion Report (2026-03-03)

## Scope

This update focuses on the Unity-side MIQ SDK hardening track and gate stability:

- Add a non-throwing runtime loader API with structured diagnostics.
- Strengthen runtime parser boundary/schema validation while preserving MIQ v1 compatibility.
- Improve exporter manifest default/required-field hygiene.
- Expand fixed-valid MIQ gate input coverage via VRM-derived generation.
- Synchronize user-facing documentation and changelog entries.

## Implemented Changes

### 1) Runtime diagnostics API (`Load` + `TryLoad`)

Files:

- `unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`
- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`

Changes:

- Added `MiqLoadErrorCode` with explicit categories:
  - `None`
  - `IoError`
  - `MagicMismatch`
  - `UnsupportedVersion`
  - `ManifestTruncated`
  - `ManifestInvalid`
  - `MissingRequiredManifestKeys`
  - `SectionHeaderTruncated`
  - `SectionTruncated`
  - `SectionSchemaInvalid`
- Added `MiqLoadDiagnostics`:
  - `ErrorCode`
  - `ErrorMessage`
  - `ParserStage`
  - `IsPartial`
  - `Warnings`
- Added `MiqRuntimeLoader.TryLoad(path, out payload, out diagnostics)`:
  - returns `false` instead of throwing on parse failure.
  - records stage-aware diagnostics (`parse`, `resolve`, `payload`, `runtime-ready`).
- Preserved `MiqRuntimeLoader.Load(path)` behavior:
  - now wraps `TryLoad` and throws with diagnostic context on failure.

### 2) Runtime parser hardening + compatibility retention

File:

- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`

Changes:

- Reworked parser to validate all boundary-sensitive reads:
  - header magic/version
  - manifest size/range
  - section header and payload ranges
  - variable-length field payloads
- Added section-level schema validation for:
  - mesh (`0x0011`)
  - texture (`0x0002`)
  - material override (`0x0003`)
  - material shader params (`0x0012`)
  - skin (`0x0013`)
  - blendshape (`0x0014`)
- Kept backward-compatible material decode path:
  - accepts payloads with and without `shaderVariant`.
- Unknown section handling remains non-breaking:
  - section is skipped and warning is recorded.
- Added partial-compat classification signals through diagnostics:
  - ref/payload mismatch warnings for mesh/texture/material surfaces.

### 3) Exporter manifest hygiene + strict shader message normalization

File:

- `unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`

Changes:

- Centralized default constants:
  - `schemaVersion=1`
  - `exporterVersion=0.3.0`
- Ensured required manifest containers are initialized:
  - `meshRefs`, `materialRefs`, `textureRefs`, `strictShaderSet`
- Ensured baseline manifest identity defaults:
  - `avatarId`, `displayName`, `sourceExt`
- Added ref-population fallback:
  - if `meshRefs/materialRefs/textureRefs` are empty, populate from payload sets.
- Standardized strict shader policy exception text to include:
  - material name
  - shader name

### 4) Gate input expansion from VRM-derived fixed MIQ samples

Files:

- `tools/vxavatar_sample_report.ps1`
- `tools/vxavatar_quality_gate.ps1`

Changes:

- Added `-FixedMiqFromVrmCount` (default `5`) to sample report script.
- Added `Add-GeneratedMiqFromVrm` helper:
  - selects up to N `.vrm` samples.
  - generates `.miq` via `vrm_to_miq`.
  - registers generated files as `fixed-valid` MIQ inputs.
- Added matching pass-through option in quality gate script.
- Existing synthetic-corrupt MIQ generation and Gate F/G contracts remain unchanged.

## Documentation Sync

Files:

- `README.md`
- `unity/Packages/com.animiq.miq/README.md`
- `CHANGELOG.md`

Changes:

- Documented `TryLoad` diagnostics usage in project/package readmes.
- Added fixed MIQ generation policy note in VXAvatar/VXA2/MIQ gate section.
- Added changelog entry summarizing this implementation slice.

## Verification

Executed:

- `powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`

Result:

- Gate A/B/C/D/E/F/G = `PASS`
- Overall = `PASS`
- Coverage snapshot:
  - `FixedMIQ=5`
  - `CorruptMIQ=2`

## Compatibility Notes

- MIQ file format version remains v1.
- Existing throw-based load path (`Load`) remains available.
- Material payload backward compatibility is preserved for legacy sections without `shaderVariant`.

## Next Steps

1. Add focused Unity tests for `TryLoad` error-code mapping (magic/version/manifest/section truncation cases).
2. Add deterministic fixed VRM sample allowlist for CI reproducibility across environments.
3. Optionally surface strict validation mode in runtime loader while keeping default non-breaking behavior.
