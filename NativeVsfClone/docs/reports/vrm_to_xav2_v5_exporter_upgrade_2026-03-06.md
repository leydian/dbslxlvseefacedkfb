# VRM to XAV2 v5 Exporter Upgrade (2026-03-06)

## Scope

This report documents the `tools/vrm_to_xav2.cpp` uplift from a minimal mesh/material exporter into a v5-capable runtime-ready exporter.

In scope:

- XAV2 file version uplift (`v1 -> v5`)
- Manifest contract hardening for skin/blendshape/physics flags
- Full payload serialization path coverage (skin/skeleton/rig/blendshape/physics)
- Section compression path and CLI operational controls
- Build and sample conversion verification

Out of scope:

- Native runtime secondary-motion solver behavior changes
- Unity editor importer/exporter codepath changes
- Release pipeline or CI script changes

## Implemented Changes

### 1) Export format/version and manifest quality

- Changed exporter output to `XAV2 v5`.
- Manifest generation now reflects actual payload state:
  - `hasSkinning`
  - `hasBlendShapes`
  - `hasSpringBones`
  - `hasPhysBones`
  - `physicsSchemaVersion`
  - `physicsSource`
  - `materialParamEncoding`
- Kept source identity fields deterministic (`avatarId`, `displayName`, refs arrays).

### 2) Payload coverage expansion

Previously, `vrm_to_xav2` wrote only:

- `0x0011` mesh render payload
- `0x0002` texture blob
- `0x0003` material override
- `0x0012` material params json

Now it additionally writes:

- `0x0013` skin payload
- `0x0014` blendshape payload
- `0x0015` material typed params
- `0x0016` skeleton pose payload
- `0x0017` skeleton rig payload
- `0x0018` springbone payload
- `0x0019` physbone payload
- `0x001A` physics collider payload

This aligns writer output with current loader/runtime section contracts.

### 3) Compression and deterministic section writing

- Added LZ4 raw block compressor in exporter tool.
- Compression is applied opportunistically for large sections:
  - mesh, texture, skin, blendshape
- Compression is only kept when section size improves (`compressed + envelope < raw`).
- Emits section flag `0x0001` and size envelope format compatible with current XAV2 loader behavior.

### 4) Validation and diagnostics

- Added pre-export validation checks:
  - duplicate collider names
  - missing collider references from spring/phys payloads
  - unresolved typed texture references
  - invalid rig local matrix size (`!= 16`)
- Added diagnostics JSON output (`--diag-json`) with:
  - payload counts
  - section counts/compressed section counts
  - source warning code snapshot
  - exporter-side validation issue list

### 5) CLI operational upgrades

New CLI usage:

`vrm_to_xav2 [--strict] [--no-compress] [--diag-json <path>] <input.vrm> <output.xav2>`

- `--strict`
  - fails export when source is not `Compat=Full`
  - fails when source warning codes or exporter validation issues exist
- `--no-compress`
  - disables v5 section compression and writes raw payload sections
- `--diag-json <path>`
  - writes machine-readable export diagnostics artifact

## Verification Summary

- Build:
  - `cmake --build NativeVsfClone/build --config Release --target vrm_to_xav2`
  - Result: PASS
- Sample conversion:
  - `vrm_to_xav2 --diag-json NativeVsfClone/build/reports/vrm_to_xav2_diag.json "sample/Kikyo_FT Variant.vrm" "build/kikyo_test.xav2"`
  - Result: PASS
  - Summary:
    - `XAV2(v5)` written
    - `Sections=208`
    - `CompressedSections=77`
    - `BlendShapes=16`
- Load validation:
  - `avatar_tool "build/kikyo_test.xav2"`
  - Result: PASS
  - Key output:
    - `Format=XAV2`
    - `Compat=full`
    - `PrimaryError=NONE`
    - `FormatDecodedSections=208`
- Strict mode sample:
  - `vrm_to_xav2 --strict "sample/Kikyo_FT Variant.vrm" "build/kikyo_test_strict.xav2"`
  - Result: PASS

## Known Risks or Limitations

- This change introduces a local LZ4 compressor implementation in the tool; while loader compatibility passed on sampled assets, broader corpus-level compression ratio and edge-case validation should be covered by gate automation.
- `--strict` currently enforces source `warning_codes` and exporter validation issues uniformly; some teams may want per-code allowlists for phased rollout.
- This report reflects tool-level changes only; no CI gate scripts were updated in this commit.

## Next Steps

1. Add a dedicated `vrm_to_xav2` parity gate script validating section counts/flags across fixed sample sets.
2. Add regression fixtures with springbone/physbone-rich avatars to assert non-zero physics section coverage.
3. Add policy hooks for strict mode allowlist/denylist by warning code family.
