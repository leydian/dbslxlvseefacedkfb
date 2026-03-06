# Avatar Coordinate Contract Unification (2026-03-06)

## Summary

Implemented a contract-first orientation/skinning handoff across loader/runtime to reduce recurring VRM/MIQ front/back and detached-part instability caused by warning-driven runtime heuristics.

Primary outcomes:

- `AvatarPackage` now carries explicit coordinate/preview contract fields,
- MIQ/VRM loaders now populate contract fields directly,
- native runtime preview yaw resolution now prioritizes package contract instead of warning-code inference,
- runtime diagnostics now expose contract yaw and transform confidence for immediate operator inspection.

## Problem Context

Observed failure shape in operator reports:

- VRM and VRM-origin MIQ could both appear visually unstable (front/back mismatch, hair/part desync symptoms),
- parse/runtime status remained successful (`runtime-ready`) but visual result still diverged,
- runtime preview yaw was influenced by warning-code combinations, making behavior sensitive to warning emission patterns.

Root concern:

- orientation policy depended on implicit runtime interpretation (`warning_codes`) rather than explicit loader contract.

## Implementation Details

### 1) `AvatarPackage` contract extension

Updated:

- `include/animiq/avatar/avatar_package.h`

Added fields:

- `mesh_space_basis`
- `asset_forward_axis`
- `asset_up_axis`
- `asset_handedness`
- `recommended_preview_yaw_deg`
- `transform_confidence`

Added enums:

- `AxisDirection`
- `CoordinateHandedness`
- `TransformConfidence`

Intent:

- shift orientation/skinning assumptions from warning interpretation to explicit package metadata.

### 2) MIQ loader contract population

Updated:

- `src/avatar/Miq_loader.cpp`

Changes:

- set safe defaults on load start:
  - forward=`-Z`, up=`+Y`, handedness=`right`,
  - confidence=`medium`,
  - recommended yaw=`0` (or `180` when `sourceExt=.vrm`).
- parse optional manifest contract keys:
  - `assetForwardAxis`
  - `assetUpAxis`
  - `assetHandedness`
  - `transformConfidence`
  - `recommendedPreviewYawDeg`
- mirror `skinSpaceBasis` into `mesh_space_basis`.
- added `ExtractIntField(...)` helper for signed manifest yaw extraction.

### 3) VRM loader contract population

Updated:

- `src/avatar/vrm_loader.cpp`

Changes:

- initialize explicit VRM contract defaults:
  - forward=`-Z`, up=`+Y`, handedness=`right`,
  - `mesh_space_basis=mesh_local`,
  - recommended yaw=`180`,
  - confidence=`medium`.
- after node-transform/skinning analysis, set contract yaw/confidence from loader-owned certainty:
  - uncertainty/conflict activity -> yaw `180`, confidence `low|medium`,
  - stable path -> yaw `0`, confidence `high`.

Intent:

- ensure yaw policy is derived once at loader time, not reconstructed from warnings at runtime.

### 4) Native runtime yaw resolver migration

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- removed warning-code driven `PreviewYawDegreesForAvatarPackage(...)` switching logic,
- resolver now uses:
  1. package contract (`transform_confidence` + `recommended_preview_yaw_deg`)
  2. legacy fallback only when contract is unavailable.
- added diagnostics fields in render pass summary:
  - `contract_preview_yaw_deg`
  - `transform_confidence`

## Verification

Build:

- `cmake --build AnimiqCore/build_plan_impl --config Release --target avatar_tool nativecore`: PASS

Gate:

- `powershell -ExecutionPolicy Bypass -File AnimiqCore/tools/vrm_quality_gate.ps1 -SampleDir sample -AvatarToolPath AnimiqCore/build_plan_impl/Release/avatar_tool.exe -Profile fixed5`: PASS (`Overall: PASS`)

Spot checks:

- `AnimiqCore/build_plan_impl/Release/avatar_tool.exe sample/개인작10-2.vrm --dump-warnings-limit=80`: PASS (`Compat: full`, `ParserStage: runtime-ready`)
- `AnimiqCore/build_plan_impl/Release/avatar_tool.exe 개인작10-2.miq --dump-warnings-limit=40`: PASS (`Compat: full`, `ParserStage: runtime-ready`)

## Scope / Compatibility Notes

- No public native C API signature change in this pass.
- Existing host-side persisted preview flip behavior remains unchanged and still applies as user override.
- This pass targets orientation-contract determinism; it does not claim full visual parity for all authored assets.
