# XAV2 Runtime Error Codes

Last updated: 2026-03-06

## Diagnostic fields

- `ErrorCode`: stable machine-readable enum
- `ErrorMessage`: human-readable message
- `ParserStage`: rough failure phase (`header`, `parse`, `resolve`, `payload`, `runtime-ready`)
- `WarningCodes`: normalized warning codes for non-fatal issues

## ErrorCode reference

- `None`
  - meaning: load succeeded.
- `IoError`
  - meaning: input path cannot be read.
  - action: verify file existence and access permissions.
- `MagicMismatch`
  - meaning: file header does not match XAV2 magic.
  - action: validate file extension/source and binary integrity.
- `UnsupportedVersion`
  - meaning: XAV2 format version is unsupported by the loader.
  - action: regenerate with supported exporter/format version.
- `ManifestTruncated`
  - meaning: manifest bytes are truncated/out-of-range.
  - action: regenerate artifact and re-check transport corruption.
- `ManifestInvalid`
  - meaning: manifest JSON parsing failed.
  - action: regenerate from exporter and inspect manifest source fields.
- `MissingRequiredManifestKeys`
  - meaning: required manifest fields are missing.
  - action: ensure `avatarId`, `meshRefs`, `materialRefs`, `textureRefs` are populated.
- `SectionHeaderTruncated`
  - meaning: section header cannot be fully parsed.
  - action: treat as corrupted payload.
- `SectionTruncated`
  - meaning: declared section payload exceeds file bounds.
  - action: regenerate payload and verify binary transfer.
- `SectionSchemaInvalid`
  - meaning: section content violates expected schema.
  - action: verify exporter/loader compatibility and section flags.
- `CompressionDecodeFailed`
  - meaning: compressed payload decode failed.
  - action: verify `v5` compression payload validity.
- `UnknownSectionNotAllowed`
  - meaning: unknown section encountered with `UnknownSectionPolicy = Fail`.
  - action: switch policy to `Warn`/`Ignore` or update loader support.
- `StrictValidationFailed`
  - meaning: strict validation policy rejected the payload.
  - action: load with strict mode off for triage, then fix source data.
- `ParityContractViolation`
  - meaning: shader/material/runtime parity contract broken.
  - action: align source avatar materials to supported policy.

## Common warning codes

- `XAV2_UNKNOWN_SECTION`
- `XAV2_SECTION_FLAGS_NONZERO`
- `XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED`
- `XAV2_MATERIAL_TYPED_UNSUPPORTED_SHADER_FAMILY`
