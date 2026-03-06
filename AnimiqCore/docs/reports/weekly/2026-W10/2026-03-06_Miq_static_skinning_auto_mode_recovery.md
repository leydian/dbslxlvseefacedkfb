# MIQ static skinning auto-mode recovery (2026-03-06)

## Summary

This update addresses a runtime-visible MIQ mesh breakage case where avatar load succeeds but render output appears collapsed/torn due to static skinning being disabled while skin payloads are present.

The fix introduces an avatar-aware auto policy for static skinning in native mesh build path:

- explicit env overrides still work (`on/off`)
- default auto behavior now enables static skinning for MIQ only when both skin and skeleton payloads are available

## Problem observed

Runtime snapshot and diagnostics showed:

- load path healthy (`PrimaryError: NONE`, `Compat: full`, `parser_stage=runtime-ready`)
- payload availability healthy (`SkinPayloads: 20`, `SkeletonPayloads: 20`)
- render warning present:
  - `W_RENDER: SKINNING_STATIC_DISABLED: skin payload detected; using original vertex positions.`

Interpretation:

- the avatar had enough data for deformation
- policy kept static skinning disabled
- renderer used original vertex positions and produced visible distortion for this asset

## Root cause

Static skinning policy in native render path was global env-gated and defaulted to off:

- no env var set -> off
- only explicit truthy env var enabled skinning

This conservative guardrail prevented risky rewrites, but also blocked required deformation for MIQ assets that depend on skin payload application for sane render geometry.

## Implementation details

File changed:

- `src/nativecore/native_core.cpp`

Key changes:

1. Added explicit env mode resolution for `ANIMIQ_MIQ_ENABLE_STATIC_SKINNING`
   - force-on: `1|true|yes|on`
   - force-off: `0|false|no|off`
   - auto: unset, `auto`, or unknown tokens

2. Added avatar-aware effective policy function
   - `ShouldApplyStaticSkinningForAvatarMeshes(const AvatarPackage&)`
   - behavior:
     - force-on: always on
     - force-off: always off
     - auto:
       - MIQ: on when both `skin_payloads` and `skeleton_payloads` exist
       - others: off

3. Wired effective policy into mesh build call path
   - `EnsureAvatarGpuMeshes(...)` computes `static_skinning_enabled` once per avatar
   - passes decision into `BuildGpuMeshForPayload(...)`
   - `BuildGpuMeshForPayload(...)` no longer re-queries env itself

4. Preserved warning and operator controls
   - `SKINNING_STATIC_DISABLED` warning still emitted when effective policy is off and skin payload exists
   - env var remains authoritative for forced global on/off

## Behavior impact

- Default runtime behavior improves for affected MIQ avatars:
  - no manual env setup needed in common valid-payload case
  - lower chance of bind-position render artifacts
- Existing operator workflows remain valid:
  - force disable for debugging/regression isolation
  - force enable for parity checks across formats

## Verification

Executed:

```powershell
cmake --build NativeAnimiq/build --config Release --target nativecore avatar_tool
NativeAnimiq/build/Release/avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.miq"
```

Results:

- Build: PASS (`nativecore`, `avatar_tool`)
- Loader smoke: PASS
  - `Format: MIQ`
  - `Compat: full`
  - `PrimaryError: NONE`
  - `ParserStage: runtime-ready`

## Notes

- This change is intentionally scoped to mesh deformation policy; it does not implement physics simulation runtime.
- Tracking-related diagnostics (`arkit52_missing` when tracking inactive) are orthogonal and unchanged.
