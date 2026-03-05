# Native Secondary Motion v1 Runtime (2026-03-06)

## Scope

This report covers native runtime execution support for XAV2 physics payloads in `nativecore` (secondary motion pass), including policy, correction, frame integration, and lifecycle handling.

In scope:

- per-avatar runtime chain state
- frame-time secondary motion update
- VRC-first chain selection policy
- auto-correction and warning code contracts
- state lifecycle reset behavior

Out of scope:

- full bone-transform solver parity against SDK internals
- advanced constraints (self-collision, complex collider primitives, high-order constraints)

## Implemented Changes

### 1) Runtime chain/state model

- Added native structs:
  - `SecondaryMotionChainRuntime`
  - `AvatarSecondaryMotionState`
- Added `CoreState.secondary_motion_states` map keyed by avatar handle.
- Tracks:
  - active/disabled/corrected chain counts
  - one-time warning emission gate
  - chain temporal state (`phase`, `velocity`, `offset`)

### 2) VRC-first + auto-correction policy

- PhysBone-first behavior:
  - when both PhysBone and SpringBone payloads exist, SpringBone chains are disabled by policy.
- Automatic correction:
  - numeric clamp for invalid values:
    - radius, stiffness, drag, gravity-y
  - missing `bone_paths` fallback from `root_bone_path`
  - unresolved collider refs flagged as corrected
  - unresolved target mesh mapping disables the chain safely
- Warning contracts:
  - `XAV2_PHYSICS_AUTO_CORRECTED`
  - `XAV2_PHYSICS_CHAIN_DISABLED`

### 3) Frame pass integration

- Inserted secondary motion apply step into render loop:
  - order: expression morph -> secondary motion -> draw batching
- Uses bounded `delta_time_seconds` (`1/240 .. 1/15`) for stable updates.
- Applies per-chain offset deformation on dynamic vertex buffers each frame.

### 4) Lifecycle consistency

- Clears/resets secondary motion runtime state on:
  - `nc_initialize`
  - `nc_shutdown`
  - `nc_load_avatar` (new handle state reset)
  - `nc_unload_avatar`
  - `nc_create_render_resources`
  - `nc_destroy_render_resources`

## Verification Summary

- Executed:
  - `cmake --build NativeVsfClone\build --config Release --target nativecore`
- Outcome:
  - build PASS (`nativecore.dll` produced)
- Not executed in this shell:
  - full visual A/B playback parity suite
  - stress/perf gate automation with representative multi-avatar sets

## Known Risks or Limitations

- Current implementation is a pragmatic v1 deformation pass, not a full skeletal constraint solver.
- Bone-to-mesh target mapping is heuristic (token-based from bone path/mesh name) and may underfit unusual naming schemes.
- Diagnostics are warning-oriented; hard-fail policy for malformed physics runtime data is intentionally avoided in this slice.

## Next Steps

1. Replace heuristic mesh targeting with explicit rig-bound influence mapping.
2. Add deterministic runtime fixtures for PhysBone-only / SpringBone-only / mixed cases and automate pass/fail gates.
3. Expand solver with additional constraint types while preserving current non-fatal fallback contracts.
