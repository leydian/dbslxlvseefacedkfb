# MIQ Expression/ArmPose/RealtimeShadow Recovery Update (2026-03-06)

## Summary

This update resolves the clustered runtime symptom where:

- upper-arm pose controls appeared non-functional
- realtime shadow appeared missing
- facial expressions appeared non-functional

Root causes were split across data and policy:

- MIQ loads could end with `ExpressionCount=0` despite available blendshape payloads.
- Arm pose policy hard-disabled VRM-origin MIQ by `sourceExt == ".vrm"` in auto mode.
- Shadow disable reasons were not explicitly surfaced when quality/profile/material gates blocked shadow draws.

Implemented outcome:

- MIQ loader now synthesizes expression catalog/binds from blendshape payloads.
- Native arm-pose gate now relies on payload completeness instead of VRM-origin extension check.
- Runtime fallback injects minimal expressions for empty MIQ expression catalogs.
- Render path now emits explicit warning codes for shadow disabled reasons.
- Export gate blocks VRM->MIQ output where blendshapes exist but expression catalog is empty.

## Problem Details

Observed sample line (`개인작10-2.miq`, local repro):

- `Format=MIQ`, `Compat=full`, `PrimaryError=NONE`
- `SkinPayloads>0`, `SkeletonPayloads>0`
- `ExpressionCount=0`
- warning included `MIQ_BLENDSHAPE_PARTIAL`

Impact:

- expression morph loop early-exited because `avatar_pkg.expressions.empty()`
- arm pose was policy-blocked for VRM-origin MIQ in auto mode
- shadow state required inference from pass counters without explicit reason diagnostics

## Implementation

### 1) MIQ loader expression synthesis

Updated:

- `src/avatar/miq_loader.cpp`

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
  - `W_PAYLOAD: MIQ_EXPRESSION_CATALOG_SYNTHESIZED`
  - `W_PAYLOAD: MIQ_EXPRESSION_CATALOG_EMPTY`

Result:

- MIQ files with usable blendshape frames no longer collapse to `ExpressionCount=0`.

### 2) Native arm pose policy correction

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- In `ShouldApplyArmPoseForAvatar(...)`, removed auto-mode hard block:
  - removed: `source_type == Miq && source_ext == ".vrm" => false`
- Auto-mode arm pose now remains payload-driven:
  - requires MIQ + non-empty skin/skeleton/rig payload sets

Result:

- VRM-origin MIQ can apply upper-arm pose when required rig payloads are present.

### 3) Runtime expression fallback extension (MIQ)

Updated:

- `src/nativecore/native_core.cpp` (`nc_load_avatar` path)

Changes:

- Extended empty-expression fallback from VRM-only to VRM + MIQ.
- Injects baseline expressions:
  - `blink`
  - `aa` (`viseme_aa`)
  - `joy`
- Added MIQ-specific runtime diagnostics:
  - warning message `W_MIQ_EXPRESSION_FALLBACK`
  - warning code `MIQ_EXPRESSION_FALLBACK_APPLIED`

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

### 5) VRM->MIQ exporter hard gate

Updated:

- `tools/vrm_to_miq.cpp`

Changes:

- Added hard validation issue:
  - `MIQ_EXPRESSION_CATALOG_EMPTY` when blendshape payload exists but expression catalog is empty.
- Added early export failure on same condition before write phase.
- Added this code to strict hard-reject and quality summary P0 sets.

Result:

- Prevents generating MIQ artifacts that would load with expression catalog loss.

## Verification

Executed:

```powershell
cmake --build NativeAnimiq/build --config Release --target avatar_tool nativecore
NativeAnimiq/build/Release/avatar_tool.exe D:\dbslxlvseefacedkfb\개인작10-2.miq
```

Observed:

- build succeeded for both `avatar_tool` and `nativecore`
- `개인작10-2.miq` moved from prior `ExpressionCount=0` to:
  - `ExpressionCount: 52`
  - `ExpressionBindTotal: 57`
- warning code now includes:
  - `MIQ_EXPRESSION_CATALOG_SYNTHESIZED`

Additional sample sweep (local `.miq` set):

- previously zero-expression samples now report non-zero expression counts.

## Compatibility / Risk Notes

- API signatures unchanged.
- Added warning codes may appear in existing diagnostics pipelines and should be treated as additive.
- Expression synthesis uses heuristic name mapping (`blink`, `aa`, `joy`, fallback by normalized frame key); this restores operability and should be superseded by explicit authored expression metadata when available.
- Arm-pose enablement remains guarded by existing payload validity checks and skinning guards.
