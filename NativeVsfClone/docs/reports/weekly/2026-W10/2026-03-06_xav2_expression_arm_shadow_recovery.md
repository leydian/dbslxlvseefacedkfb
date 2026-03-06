# XAV2 Expression/ArmPose/RealtimeShadow Recovery Update (2026-03-06)

## Summary

This update resolves the clustered runtime symptom where:

- upper-arm pose controls appeared non-functional
- realtime shadow appeared missing
- facial expressions appeared non-functional

Root causes were split across data and policy:

- XAV2 loads could end with `ExpressionCount=0` despite available blendshape payloads.
- Arm pose policy hard-disabled VRM-origin XAV2 by `sourceExt == ".vrm"` in auto mode.
- Shadow disable reasons were not explicitly surfaced when quality/profile/material gates blocked shadow draws.

Implemented outcome:

- XAV2 loader now synthesizes expression catalog/binds from blendshape payloads.
- Native arm-pose gate now relies on payload completeness instead of VRM-origin extension check.
- Runtime fallback injects minimal expressions for empty XAV2 expression catalogs.
- Render path now emits explicit warning codes for shadow disabled reasons.
- Export gate blocks VRM->XAV2 output where blendshapes exist but expression catalog is empty.

## Problem Details

Observed sample line (`개인작10-2.xav2`, local repro):

- `Format=XAV2`, `Compat=full`, `PrimaryError=NONE`
- `SkinPayloads>0`, `SkeletonPayloads>0`
- `ExpressionCount=0`
- warning included `XAV2_BLENDSHAPE_PARTIAL`

Impact:

- expression morph loop early-exited because `avatar_pkg.expressions.empty()`
- arm pose was policy-blocked for VRM-origin XAV2 in auto mode
- shadow state required inference from pass counters without explicit reason diagnostics

## Implementation

### 1) XAV2 loader expression synthesis

Updated:

- `src/avatar/xav2_loader.cpp`

Changes:

- Added morph-key normalization and expression synthesis helpers:
  - `NormalizeMorphKey(...)`
  - `AddExpressionIfMissing(...)`
  - `FindExpressionByName(...)`
  - `AddExpressionBindIfMissing(...)`
  - `ResolveExpressionFromFrameName(...)`
  - `BuildExpressionCatalogFromBlendShapes(...)`
- Added synthesis pass before payload stage completion:
  - build expression entries from `blendshape_payloads[*].frames[*]`
  - generate binds per mesh/frame with clamped weight scale
- Added warnings:
  - `W_PAYLOAD: XAV2_EXPRESSION_CATALOG_SYNTHESIZED`
  - `W_PAYLOAD: XAV2_EXPRESSION_CATALOG_EMPTY`

Result:

- XAV2 files with usable blendshape frames no longer collapse to `ExpressionCount=0`.

### 2) Native arm pose policy correction

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- In `ShouldApplyArmPoseForAvatar(...)`, removed auto-mode hard block:
  - removed: `source_type == Xav2 && source_ext == ".vrm" => false`
- Auto-mode arm pose now remains payload-driven:
  - requires XAV2 + non-empty skin/skeleton/rig payload sets

Result:

- VRM-origin XAV2 can apply upper-arm pose when required rig payloads are present.

### 3) Runtime expression fallback extension (XAV2)

Updated:

- `src/nativecore/native_core.cpp` (`nc_load_avatar` path)

Changes:

- Extended empty-expression fallback from VRM-only to VRM + XAV2.
- Injects baseline expressions:
  - `blink`
  - `aa` (`viseme_aa`)
  - `joy`
- Added XAV2-specific runtime diagnostics:
  - warning message `W_XAV2_EXPRESSION_FALLBACK`
  - warning code `XAV2_EXPRESSION_FALLBACK_APPLIED`

Result:

- Expression submission paths stay operational even when source catalog is unexpectedly empty.

### 4) Shadow-disabled reason diagnostics

Updated:

- `src/nativecore/native_core.cpp` (render queue build path)

Changes:

- Added per-avatar shadow state markers while building draw queues:
  - whether any material supports shadow pass
  - whether shadow draw items were enqueued
- Added explicit warning codes:
  - `SHADOW_DISABLED_TOGGLE_OFF`
  - `SHADOW_DISABLED_FAST_FALLBACK`
  - `SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL`
  - `SHADOW_DISABLED_SHADOW_DRAW_EMPTY`

Result:

- Missing realtime shadow now reports a concrete disable reason instead of requiring pass-level inference.

### 5) VRM->XAV2 exporter hard gate

Updated:

- `tools/vrm_to_xav2.cpp`

Changes:

- Added hard validation issue:
  - `XAV2_EXPRESSION_CATALOG_EMPTY` when blendshape payload exists but expression catalog is empty.
- Added early export failure on same condition before write phase.
- Added this code to strict hard-reject and quality summary P0 sets.

Result:

- Prevents generating XAV2 artifacts that would load with expression catalog loss.

## Verification

Executed:

```powershell
cmake --build NativeVsfClone/build --config Release --target avatar_tool nativecore
NativeVsfClone/build/Release/avatar_tool.exe D:\dbslxlvseefacedkfb\개인작10-2.xav2
```

Observed:

- build succeeded for both `avatar_tool` and `nativecore`
- `개인작10-2.xav2` moved from prior `ExpressionCount=0` to:
  - `ExpressionCount: 52`
  - `ExpressionBindTotal: 57`
- warning code now includes:
  - `XAV2_EXPRESSION_CATALOG_SYNTHESIZED`

Additional sample sweep (local `.xav2` set):

- previously zero-expression samples now report non-zero expression counts.

## Compatibility / Risk Notes

- API signatures unchanged.
- Added warning codes may appear in existing diagnostics pipelines and should be treated as additive.
- Expression synthesis uses heuristic name mapping (`blink`, `aa`, `joy`, fallback by normalized frame key); this restores operability and should be superseded by explicit authored expression metadata when available.
- Arm-pose enablement remains guarded by existing payload validity checks and skinning guards.
