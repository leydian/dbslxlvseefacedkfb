# XAV2 SDK Compatibility Matrix

Last updated: 2026-03-06

## Supported runtime baseline

- Package: `com.vsfclone.xav2@1.0.0`
- Unity Editor: `2021.3.18f1`
- Render pipeline: Built-in Render Pipeline only
- Platform expectation: Windows-first validation baseline

## Shader support policy

Allowed shader families:

- `Standard`
- `MToon`
- `lilToon`
- `Poiyomi`

Default behavior:

- export fails on unsupported shader family when `FailOnMissingShader = true`
- runtime load fails with `ParityContractViolation` for unsupported typed shader family

## XAV2 format support

- Export:
  - `v4` (no section compression)
  - `v5` (section-level compression when enabled)
- Load:
  - `v1`, `v2`, `v3`, `v4`, `v5`

Compression support:

- codec: `LZ4` (`Xav2CompressionCodec.Lz4`)
- unknown section handling defaults to `Warn`

## Out of scope for 1.0.0

- URP/HDRP rendering support
- multi-Unity baseline support outside `2021.3.18f1`
- non-Windows parity guarantees
