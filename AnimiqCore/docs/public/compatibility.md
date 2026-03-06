# MIQ SDK Compatibility Matrix

Last updated: 2026-03-06

## Supported runtime baseline

- Package: `com.animiq.miq@1.0.0`
- Unity Editor (gate-backed official lines):
  - `2021.3.18f1` (`2021-lts`)
  - `2022.3.62f1` (`2022-lts`)
  - `2023.2.20f1` (`2023-lts`)
- Minimum package floor: `unity: 2021.3`, `unityRelease: 18f1`
- Render pipeline: Built-in Render Pipeline only
- Platform expectation: Windows-first validation baseline

## Shader support policy

Allowed shader families:

- `Standard`
- `MToon`
- `lilToon`
- `Poiyomi`

Default behavior:

- export defaults to relaxed path (`Export Selected AvatarRoot`), strict export is explicit menu entry
- runtime load defaults to `ShaderPolicy = WarnFallback`
- strict parity behavior is available via `ShaderPolicy = Fail` and is used by CI parity gate

## MIQ format support

- Export:
  - `v4` (no section compression)
  - `v5` (section-level compression when enabled)
- Load:
  - `v1`, `v2`, `v3`, `v4`, `v5`

Compression support:

- codec: `LZ4` (`MiqCompressionCodec.Lz4`)
- unknown section handling default: `Warn`

## Official support contract

A Unity LTS line is considered officially supported only when all gates pass for that line:

- EditMode tests
- export/load smoke
- compression quality gate
- unity/native parity gate

Gate orchestration is matrix-driven through:

- `.github/workflows/unity-miq-compat.yml`
- `tools/unity_lts_matrix.json`

## Out of scope for 1.0.0

- URP/HDRP rendering support
- non-Windows parity guarantees
