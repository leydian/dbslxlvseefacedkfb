# VRM Node Transform Skinning Unification + Preview Yaw Auto Fallback (2026-03-06)

## Summary

This pass addresses a repeated VRM direct-load failure shape:

- avatar loads to `runtime-ready` but appears visually broken/distorted (skin deformation mismatch),
- preview may start from an unintended back-facing orientation.

The fix unifies node-transform handling for skinned meshes in the VRM loader and introduces package-aware preview yaw auto selection in native runtime.

Primary outcomes:

- removed default skinned-mesh node-transform bypass path,
- aligned skinning-space behavior when node transform is already baked into vertices,
- added explicit conflict-only fallback warning contract,
- added VRM preview yaw auto fallback (`0/180`) with diagnostics reason output.

## Problem Context

Observed operator symptom:

- VRM import succeeds (`PrimaryError=NONE`, `ParserStage=runtime-ready`) but body/head can look broken.
- In affected samples, warning line often involved node-transform + skin interaction.
- Front/back orientation was inconsistent for some assets in preview path.

Root issue line:

- loader previously skipped node-transform application for skinned meshes, while skin skeleton conversion still used mesh-space transform assumptions, producing inconsistent transform basis in some assets.

## Implementation Details

### 1) VRM loader: skinned node-transform path unification

Updated:

- `src/avatar/vrm_loader.cpp`

Changes:

- Replaced `mesh_node_transform_skin_bypass` tracking with `mesh_node_transform_applied`.
- Node transform is now applied to mesh vertex position stream regardless of skinned/non-skinned path when available.
- Skeleton conversion logic now branches by whether node transform was already applied:
  - if applied: keep joint global pose as-is for skin matrix path,
  - if not applied (conflict/no-transform path): keep prior mesh-space conversion (`mesh_inv * joint`).
- Updated skinning convention diagnostic text:
  - `VRM_SKINNING_CONVENTION: globalJoint*inverseBind`
- Added conflict-only fallback warning code:
  - `VRM_NODE_TRANSFORM_SKIN_FALLBACK`
- Removed default warning contract dependency on:
  - `VRM_NODE_TRANSFORM_SKIN_BYPASS`

Behavior intent:

- default path now avoids transform-space mismatch between baked vertex positions and skeleton matrices,
- fallback warning remains only for true conflict cases.

### 2) Native runtime: VRM preview yaw auto fallback

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Added package-aware yaw resolver:
  - `PreviewYawDegreesForAvatarPackage(...)`
  - `PreviewYawRadiansForAvatarPackage(...)`
- Added warning-code probe helper:
  - `HasWarningCode(...)`
- VRM yaw decision policy:
  - use default yaw baseline first,
  - auto-fallback to `180` when node-transform conflict/fallback signals are present,
  - auto-fallback to `180` when node-transform-applied + skinned payload combination is detected.
- Render/diagnostics now expose decision metadata:
  - `applied_preview_yaw_deg=<...>`
  - `preview_yaw_reason=<...>`

Operational intent:

- reduce back-facing startup across mixed VRM assets without changing public API.

## Verification Snapshot

Executed:

```powershell
cmake --build NativeAnimiq\build --config Release --target nativecore avatar_tool
powershell -ExecutionPolicy Bypass -File NativeAnimiq\tools\vrm_quality_gate.ps1 `
  -SampleDir sample `
  -AvatarToolPath NativeAnimiq\build\Release\avatar_tool.exe `
  -Profile fixed5
```

Observed:

- `nativecore` build: PASS
- `avatar_tool` build: PASS
- VRM gate (`fixed5`): PASS (GateA..GateL all pass in current policy mode)
- Probe output no longer shows `VRM_NODE_TRANSFORM_SKIN_BYPASS` in tested fixed5 set; node-transform warning path is now `VRM_NODE_TRANSFORM_APPLIED` on applicable samples.

## Scope Notes

- This pass targets transform-space consistency and preview orientation reliability for VRM direct-load path.
- It does not introduce full VRM SDK-equivalent bone-constraint parity.
- Host UI/UX controls for manual preview-yaw override are not changed in this pass.

