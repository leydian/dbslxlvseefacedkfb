# VRM Crash Guard + Front Yaw + Cull Hotfix (2026-03-06)

## Summary

This hotfix sequence closes the final VRM direct-load operator issues observed in production-like usage:

- app process exited while loading specific VRM assets,
- avatar started in back-facing view by default,
- clothing/body looked broken or "transparent" after yaw correction.

The fix was delivered as a focused runtime/loader stabilization line with no public C API change.

Primary outcomes:

- invalid node-transform application now fails safe (no crash-inducing vertex corruption),
- VRM preview defaults to front-facing orientation,
- VRM render path now avoids face-culling loss that manifested as transparent/missing outfit surfaces.

## Problem Context

### 1) Crash on VRM load

After enabling skinned node-transform unification, some assets still had unstable node matrices in edge cases.
Applying those transforms directly to vertex streams could produce invalid coordinates and destabilize runtime behavior.

### 2) Back-facing default preview

Runtime diagnostics showed host loaded `dist/wpf/nativecore.dll` path and preview remained back-facing in operator flow.
A deterministic front-facing default was required for VRM path.

### 3) Outfit appears transparent/broken

`avatar_tool` diagnostics for affected sample (`sample/개인작11-3.vrm`) showed:

- `BlendMaterials: 0`
- mixed `OPAQUE`/`MASK` materials

This indicated the visual break was not classic alpha-blend transparency but render-face loss/culling behavior.

## Implementation Details

### A) Loader safety guard for node-transform application

Updated:

- `src/avatar/vrm_loader.cpp`

Changes:

- `ApplyPositionTransformToVertexBlob(...)` now returns `bool` success/failure.
- Added matrix input guard:
  - reject non-finite values,
  - reject extreme magnitude values (`abs(v) > 1e6`).
- Added transformed-position guard:
  - reject non-finite/over-range transformed xyz.
- Switched to temporary transformed buffer and commit-on-success pattern (atomic apply).
- On failure, skip transform and emit:
  - warning: `W_NODE: VRM_NODE_TRANSFORM_INVALID: ... action=skipped`
  - warning code: `VRM_NODE_TRANSFORM_INVALID`

Result:

- invalid transform payload can no longer partially poison vertex stream.

### B) VRM front-facing preview default

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- `PreviewYawDegreesForAvatarSource(...)` now returns:
  - `XAV2 => 0`
  - `VRM => 180`
  - others unchanged (`180`)

Result:

- VRM starts in front-facing orientation by default.

### C) VRM culling hotfix for outfit loss/transparent look

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Render draw pass no-cull override expanded:
  - from `XAV2` only
  - to `XAV2 || VRM`
- This applies for both base and outline pass state selection paths.

Result:

- VRM outfit/body surfaces no longer disappear due to face-cull mismatch after orientation correction.

## Verification Snapshot

Executed:

```powershell
cmake --build NativeVsfClone\build --config Release --target nativecore avatar_tool
powershell -ExecutionPolicy Bypass -File NativeVsfClone\tools\vrm_quality_gate.ps1 `
  -SampleDir sample `
  -AvatarToolPath NativeVsfClone\build\Release\avatar_tool.exe `
  -Profile fixed5
```

Observed:

- build (`nativecore`, `avatar_tool`): PASS
- VRM quality gate (`fixed5`): PASS
- affected sample probe (`sample/개인작11-3.vrm`) remains `runtime-ready` with no critical warning code.

Runtime path note:

- operator diagnostics initially showed loaded module path fixed to:
  - `NativeCoreModulePath: ...\dist\wpf\nativecore.dll`
- dist binary had stale hash/timestamp compared to `build\Release\nativecore.dll`.
- post-hotfix deployment aligned dist DLL with latest build output.

## Scope Notes

- This pass is a targeted stability/visibility hotfix for VRM path.
- It does not implement full physically-correct two-sided material semantics per shader family.
- It prioritizes "never crash + always visible + front-facing default" operational behavior.

