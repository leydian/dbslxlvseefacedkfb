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

## Host troubleshooting flow (action contract)

Use the same sequence for support and self-recovery:

1. Check `ErrorCode` and `WarningCodes` in diagnostics/runtime status.
2. Apply the mapped remediation action in this document.
3. Export diagnostics bundle and run `repro_commands.txt`.
4. Re-run with the same input and compare results.

Diagnostics bundle includes:

- `repro_commands.txt`
- `environment_snapshot.json`
- `telemetry.json`
- `onboarding_kpi_summary.txt`

## Common host tracking/runtime codes

- `TRACKING_PARSE_THRESHOLD_EXCEEDED`
  - meaning: parse errors exceeded configured warning threshold.
  - action: validate source packet schema and channel mapping.
- `TRACKING_DROP_THRESHOLD_EXCEEDED`
  - meaning: dropped packets exceeded threshold.
  - action: check network stability and source send rate.
- `TRACKING_NO_MAPPED_CHANNELS`
  - meaning: source packets arrived but no mapped channels were usable.
  - action: verify channel map and source profile compatibility.
- `TRACKING_MEDIAPIPE_CONFIG_INVALID`
  - meaning: webcam runtime config invalid (sidecar path/script mismatch).
  - action: verify `mediapipe_webcam_sidecar.py` location and runtime settings.
- `TRACKING_MEDIAPIPE_START_FAILED`
  - meaning: webcam sidecar process failed to start.
  - action: verify Python runtime and dependency availability.
- `TRACKING_MEDIAPIPE_NO_FRAME`
  - meaning: sidecar started but produced no frames.
  - action: verify camera permissions/device and sidecar logs.
- `TRACKING_NO_ACTIVE_INPUT_SOURCE`
  - meaning: no active iFacial/webcam source detected.
  - action: start source stream and confirm host source lock mode.
- `TRACKING_IFACIAL_NO_PACKET`
  - meaning: iFacial source active path has no incoming packet.
  - action: check sender endpoint, host listen port, and firewall.
- `TRACKING_WEBCAM_RUNTIME_UNAVAILABLE`
  - meaning: webcam runtime unavailable.
  - action: verify Python + MediaPipe runtime readiness.
- `TRACKING_WEBCAM_NO_FRAME`
  - meaning: webcam runtime active but no frame accepted.
  - action: lower inference fps cap and validate camera health.
