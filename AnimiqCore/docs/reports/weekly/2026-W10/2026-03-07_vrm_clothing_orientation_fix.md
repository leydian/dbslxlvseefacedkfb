# VRM Clothing Orientation Fix (2026-03-07)

## Scope

Fixes the 180° orientation mismatch introduced by commit `e3010cb` where hair faced the viewer correctly but clothing (skinned meshes with a 180° Y-rotation mesh node) faced away. Covers `vrm_loader.cpp` position-baking and normal-rotation logic only; no animation, bone hierarchy, or shader changes.

## Root Cause

`e3010cb` added `&& !is_skinned` to the position-baking guard:

```cpp
// Broken (e3010cb):
if (has_node_transform && !is_skinned) {
```

This caused skinned meshes to skip `ApplyPositionTransformToVertexBlob`, leaving `mesh_node_transform_applied = false`. The fallback path at line ~3592 then applied `mesh_inv_for_skin = inverse(mesh_node_global_transform)` to every bone matrix. For clothing whose mesh node has a 180° Y-rotation, this removed that rotation from bone matrices. Hair had an identity mesh node transform, so its bone matrices kept their world-space orientation — producing a 180° mismatch between hair and clothing.

## Implemented Changes

### Change 1 — Restore position baking for skinned meshes (`vrm_loader.cpp` line 3418)

```cpp
// Fixed:
if (has_node_transform) {
```

With baking re-enabled for all mesh types, `mesh_node_transform_applied[mesh_i]` becomes `true` for skinned meshes too. The `mesh_inv_for_skin` multiplication at line ~3592 is then skipped (`if (!has_node_transform_applied)` is false). Both hair and clothing bone matrices remain in world space — same convention — eliminating the mismatch.

### Change 2 — Rotate normals to match baked positions for unskinned meshes (`vrm_loader.cpp` after line 3450)

When an unskinned mesh's positions are baked into world space, the VRM normals (still in local space) become inconsistent. Added a rotation pass after normal extraction:

- Extracts the upper-left 3×3 of `mesh_node_transforms[mesh_i]`
- Normalizes each column to isolate the pure rotation (handles non-unit uniform scale)
- Applies the rotation to every normal in `vrm_normals` and renormalizes
- Guard: `!is_skinned && mesh_node_transform_applied[mesh_i]` — skinned meshes are excluded because the renderer's skinning shader handles normal orientation at runtime

## Verification Steps

1. Load the "블러움" avatar — clothing and hair should both face the viewer
2. Confirm no vertex distortion in clothing mesh in T-pose and in motion
3. Load other VRM files to check for regressions (especially models with explicit unskinned/static meshes)
4. Check `VRM_NODE_TRANSFORM_APPLIED` warnings — should now appear for both skinned and unskinned mesh types

## Known Risks or Limitations

- If a skinned mesh's node transform contains a **non-uniform scale**, the position-baking step (`ApplyPositionTransformToVertexBlob`) will apply it to vertices. This is the same behavior as before `e3010cb` and was not a reported issue, but worth monitoring on models with unusual scale setups.
- The uncommitted change at lines 2754–2758 (unconditional `mesh_has_node_transform = true`, removing the `IsIdentityMatrix4x4` guard) is preserved. It inflates the `VRM_NODE_TRANSFORM_APPLIED` warning count slightly for identity-transform meshes but is otherwise harmless.

## Next Steps

- Monitor for vertex distortion regressions across a broader set of VRM files
- If non-uniform scale distortion is observed, investigate separating the scale factor from the baking step for skinned meshes
