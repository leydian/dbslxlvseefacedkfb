# MIQ render breakage guardrail update (2026-03-06)

## Summary

This update applies a focused stabilization pass for the MIQ/lilToon render-breakage line by reducing high-risk deformation behavior in native render, normalizing typed material texture resolution, and strengthening diagnostics parity between native and Unity runtime loader paths.

Primary intent:

- stop shape-collapse regressions caused by risky static skinning assumptions
- make typed-v2 material texture binding more robust across path/case variations
- surface unresolved typed texture references as explicit warning contracts

## Problem context

Recent MIQ changes introduced:

- static skinning application in native mesh upload path
- typed-v2 (`0x0015`) material parameter flow

While these were directionally correct, two practical issues remained:

1. Static skinning path could still produce visible deformation artifacts depending on bind-pose assumptions.
2. Typed texture references could fail to bind due to key normalization mismatches (case/path separator differences), with weak visibility into root cause.

## Implemented changes

### 1) Native render path: static skinning guardrail

File:

- `src/nativecore/native_core.cpp`

Changes:

- Added opt-in toggle for static skinning application:
  - env var: `ANIMIQ_MIQ_ENABLE_STATIC_SKINNING`
  - accepted truthy values: `1`, `true`, `yes`, `on`
- Default behavior now keeps static skinning disabled to prioritize non-destructive rendering.
- When skin payload exists but static skinning is disabled, native path now records warning:
  - `W_RENDER: MIQ_SKINNING_STATIC_DISABLED: ...`
  - warning code: `MIQ_SKINNING_STATIC_DISABLED`

Effect:

- reduces collapse/distortion risk from aggressive mesh-space rewriting
- preserves an opt-in route for controlled experimentation

### 2) Native material path: typed texture resolution hardening

File:

- `src/nativecore/native_core.cpp`

Changes:

- Added normalized key utility for texture/slot comparisons:
  - lower-case compare
  - `\` to `/` normalization
- Typed texture slot lookup now uses normalized matching.
- Texture payload lookup now uses normalized map instead of exact string equality.
- When typed `base` texture reference is present but unresolved, native path emits:
  - `W_RENDER: MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED: ...`
  - warning code: `MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED`

Effect:

- improves typed-v2 texture bind success rate for real-world asset naming/path differences
- makes unresolved texture causes directly observable in diagnostics

### 3) Native alpha mode resolution safety

File:

- `src/nativecore/native_core.cpp`

Changes:

- `feature_flags`-driven alpha override (`MASK`/`BLEND`) now applies only when `material_param_encoding == typed-v2`.
- legacy-json path remains resolved by existing shader-params heuristics.

Effect:

- avoids unintended alpha-mode override in non-typed material payloads

### 4) Native loader diagnostics parity for typed textures

File:

- `src/avatar/miq_loader.cpp`

Changes:

- Added post-material validation pass:
  - checks every typed texture ref against manifest texture refs (normalized)
  - emits warning for unresolved refs:
    - `W_PAYLOAD: MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED: ...`

Effect:

- unresolved typed texture contract is now visible at load stage, not only render stage

### 5) Unity runtime loader parity + test coverage

Files:

- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
- `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

Changes:

- Runtime loader partial-compat evaluation now normalizes mesh/texture keys before ref checks.
- Added typed texture unresolved warning emission:
  - `MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED: ...`
- Added unit test:
  - `TryLoad_TypedMaterialTextureRefMissing_Warns`
  - verifies warning text + warning code capture.

Effect:

- keeps Unity runtime diagnostics aligned with native loader expectations
- prevents regressions in typed texture warning contract

## Verification

Executed:

```powershell
cmake --build NativeAnimiq\build --config Release --target nativecore avatar_tool
NativeAnimiq\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.miq"
```

Observed:

- Build succeeded for `nativecore` and `avatar_tool`.
- Sample MIQ load remained healthy:
  - `Load succeeded`
  - `Format: MIQ`
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`

## Notes / follow-up

- This pass is a stabilization guardrail, not full skinning or full lilToon parity completion.
- If static skinning quality is revisited, re-enable via env flag first and validate with image/bbox regression checks before making it default.

