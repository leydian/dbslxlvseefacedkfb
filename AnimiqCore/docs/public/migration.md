# MIQ SDK Migration Guide

Last updated: 2026-03-06

## v0.x -> v1.0.0

## What changed

- package version moved to `1.0.0` for public SDK baseline
- public package metadata now includes repository/documentation/changelog links
- UPM samples are now bundled under `Samples~`
- compatibility contract expanded to gate-backed Unity LTS matrix:
  - `2021-lts` (`2021.3.18f1`)
  - `2022-lts` (`2022.3.62f1`)
  - `2023-lts` (`2023.2.20f1`)

## API usage status

`MiqRuntimeLoader` call patterns are unchanged:

- `Load(path)`
- `TryLoad(path, out payload, out diagnostics)`
- `TryLoad(path, out payload, out diagnostics, options)`

`MiqLoadOptions` defaults:

- `StrictValidation = false`
- `UnknownSectionPolicy = Warn`
- `ShaderPolicy = WarnFallback`

## Recommended migration checks

1. Confirm project Unity version is one of the official LTS matrix lines.
2. Confirm runtime assets use supported shader families only.
3. Replace ad-hoc loader errors with diagnostics-based handling (`ErrorCode`, `ParserStage`, `WarningCodes`).
4. Run package-level validation through EditMode tests and smoke/gate scripts for the target Unity line.

## Backward compatibility

- Loader remains backward compatible for MIQ payload versions `v1..v5`.
- Unknown section policy remains configurable through `MiqLoadOptions.UnknownSectionPolicy`.
- Strict shader parity mode remains available via `MiqLoadOptions.ShaderPolicy = Fail`.
