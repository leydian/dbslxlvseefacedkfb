# XAV2 SDK Migration Guide

Last updated: 2026-03-06

## v0.x -> v1.0.0

## What changed

- package version moved to `1.0.0` for public SDK baseline
- public package metadata now includes repository/documentation/changelog links
- UPM samples are now bundled under `Samples~`
- compatibility contract is explicitly documented for Unity `2021.3.18f1` + Built-in RP

## API usage status

`Xav2RuntimeLoader` call patterns are unchanged:

- `Load(path)`
- `TryLoad(path, out payload, out diagnostics)`
- `TryLoad(path, out payload, out diagnostics, options)`

`Xav2LoadOptions` defaults remain:

- `StrictValidation = false`
- `UnknownSectionPolicy = Warn`

## Recommended migration checks

1. Confirm project Unity version is `2021.3.18f1`.
2. Confirm runtime assets use supported shader families only.
3. Replace ad-hoc loader errors with diagnostics-based handling (`ErrorCode`, `ParserStage`, `WarningCodes`).
4. Run package-level validation through EditMode tests and smoke gate scripts before release.

## Backward compatibility

- Loader remains backward compatible for XAV2 payload versions `v1..v5`.
- Unknown section policy remains configurable through `Xav2LoadOptions.UnknownSectionPolicy`.
