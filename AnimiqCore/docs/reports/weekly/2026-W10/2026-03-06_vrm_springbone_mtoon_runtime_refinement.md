# VRM SpringBone Runtime Refinement + MToon Advanced Runtime Application (2026-03-06)

## Summary

This pass implemented the first practical runtime refinement slice for VRM compatibility uplift with two concrete goals:

- improve SpringBone behavior quality/stability beyond single-axis offset deformation
- move MToon advanced fields (outline / uv animation) from parse-only to real render-path application

The implementation keeps the current native runtime architecture and quality gates intact while raising fidelity in motion and toon rendering.

## Scope

In scope:

- `src/nativecore/native_core.cpp`
  - secondary motion runtime model/solver refinement
  - shader constant contract expansion for advanced MToon runtime behavior
  - render pass extension for outline rendering and UV animation sampling
- `src/avatar/vrm_loader.cpp`
  - SpringBone missing-feature reporting correction based on payload readiness
- gate and spot-check verification using existing fixed5 harness

Out of scope:

- full bone-joint constraint parity with dedicated VRM SDK solvers
- complete MToon keyword-level parity for all variants and edge combinations
- API v2 public contract migration

## Implemented Changes

### 1) SpringBone runtime refinement (nativecore)

File: `src/nativecore/native_core.cpp`

- Upgraded chain runtime state from scalar 1D offset to 2D offsets/velocities:
  - `velocity_x`, `velocity_y`
  - `offset_x`, `offset_y`
- Replaced oscillation-style target with tracked-head-influenced damped target solve:
  - per-substep target solve on X/Y axes
  - stiffness/drag-integrated acceleration and damping
- Added radial length constraint in 2D displacement space:
  - enforces bounded chain excursion by radius-derived limit
  - applies velocity damping on constraint hit
- Improved per-vertex deformation weighting:
  - applies displacement with height-based influence (stronger near upper region)
  - writes both X and Y deformation components (instead of Y-only)
- Collider support classification updated:
  - `Plane` no longer treated as unsupported by default classifier
  - only `Unknown` is treated unsupported for warning/counter path

### 2) MToon advanced runtime application (nativecore)

File: `src/nativecore/native_core.cpp`

- Extended GPU material runtime state with advanced fields:
  - outline: `outline_width`, `outline_lighting_mix`
  - UV animation: `uv_anim_scroll_x/y`, `uv_anim_rotation`, `uv_anim_enabled`
  - UV animation mask SRV: `uv_anim_mask_srv`
- Extended renderer pipeline resources:
  - added `raster_cull_front` for outline pass
- Expanded shader constant buffer layout:
  - `outline_params`
  - `uv_anim_params`
  - `time_params`
- Expanded shader binding slots:
  - added `tex5` for UV animation mask texture
- Added UV animation runtime sampling in pixel shader:
  - time-driven scroll + rotation
  - mask-weighted UV blend between base UV and animated UV
- Added outline rendering behavior:
  - vertex extrusion along normal in outline pass
  - separate outline tint output path in pixel shader
  - render pass insertion using front-face culling (or no-cull fallback)
- Added runtime render clock:
  - `runtime_time_seconds` advanced per frame and bounded with `fmod`

### 3) MToon typed parameter ingestion wiring (nativecore)

File: `src/nativecore/native_core.cpp`

- Added typed/fallback extraction for:
  - `_OutlineWidth`
  - `_OutlineLightingMix`
  - `_UvAnimScrollX`
  - `_UvAnimScrollY`
  - `_UvAnimRotation`
- Added typed texture ingestion for:
  - `uvAnimationMask` / `_UvAnimMaskTex`
- Added unresolved-texture warning path for UV mask parity with existing typed texture diagnostics.

### 4) SpringBone missing-feature reporting correction (vrm_loader)

File: `src/avatar/vrm_loader.cpp`

- Updated SpringBone post-parse reporting behavior:
  - when Spring metadata exists and runtime spring payloads are extracted:
    - emits readiness warning (`runtime simulation payload-ready`)
    - does **not** mark `SpringBone runtime simulation` as missing
  - when metadata exists but payload extraction remains partial:
    - keeps `SpringBone runtime simulation` in `missing_features`

This aligns diagnostics with runtime readiness and avoids over-reporting missing functionality.

## Verification

Executed on 2026-03-06:

- Build:
  - `cmake --build .\build --config Release --target nativecore avatar_tool`
  - Result: PASS

- VRM quality gate:
  - `powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5`
  - Result: PASS (Gate A..J all PASS)

- Spot checks:
  - `avatar_tool ..\sample\NewOnYou.vrm`
    - `MissingFeatures: 3`
    - Spring payload present path no longer reports Spring runtime missing
  - `avatar_tool "..\sample\Kikyo_FT Variant.vrm"`
    - `SpringPayloads: 0`
    - `MissingFeatures: 4` (partial/no payload path still correctly reported)

## Observed Impact

- Spring motion behaves less rigid and less single-axis with bounded 2D response and better stability under frame-time variation.
- MToon advanced values for outline/UV animation now affect actual rendering output rather than diagnostics-only payload carriage.
- Runtime diagnostics now better distinguish `payload-ready` vs `partial` Spring runtime capability.

## Known Limitations

- Spring solution remains a pragmatic mesh-deformation runtime, not full bone-constraint solver parity.
- Outline and UV animation implementation targets practical quality uplift, not full MToon feature-complete parity.
- API surface remains current generation; v2 contract migration is pending.
