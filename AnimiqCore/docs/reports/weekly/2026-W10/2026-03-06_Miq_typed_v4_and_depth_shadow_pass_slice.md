# MIQ typed-v4 + depth/shadow pass slice (2026-03-06)

## Summary

This slice advances the renderer-side parity foundation in two practical steps:

1. **Material contract uplift to `typed-v4`** across native + Unity data paths
2. **Render pass topology expansion** from `base/outline/emission` to include `depth/shadow`

The goal is stability-first progression toward full shader parity while preserving existing load/runtime behavior.

## What changed

### 1) typed-v4 material contract and canonicalization

Updated:

- `include/animiq/avatar/avatar_package.h`
- `src/avatar/miq_loader.cpp`
- `unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`
- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`

Changes:

- Added typed-v4-oriented material metadata fields:
  - `keyword_set`
  - `render_state`
  - `pass_flags`
- Native MIQ load canonicalization now promotes parity materials to:
  - `material_param_encoding = typed-v4`
  - `typed_schema_version >= 4`
- Default fallback values are injected when absent:
  - `shader_variant=default`
  - `keyword_set=[]`
  - `render_state=auto`
  - `pass_flags=base`
- Unity runtime/export paths were aligned to emit and preserve `typed-v4` as canonical encoding.

### 2) Render quality and diagnostics contract expansion

Updated:

- `include/animiq/nativecore/api.h`
- `host/HostCore/NativeCoreInterop.cs`
- `host/HostCore/HostController.cs`
- `src/nativecore/native_core.cpp`

Changes:

- Added quality profile:
  - `NC_RENDER_QUALITY_FAST_FALLBACK`
- Added parity diagnostics fields to `NcAvatarInfo`:
  - `parity_score`
  - `variant_id`
  - `parity_fallback_reason`
  - `quality_mode`
- Host interop/profile mapping aligned:
  - `stability` / `performance` profile routes to `FastFallback`

### 3) Native pass topology expansion (`DepthOnly` / `ShadowCaster`)

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Added material pass switches:
  - `enable_depth_pass`
  - `enable_shadow_pass`
  - existing `enable_base_pass`, `enable_outline_pass`, `enable_emission_pass` retained
- Pass enable resolution now consumes `pass_flags + keyword_set` with loose-token parsing:
  - depth tokens: `depth`, `depthonly`, `zprepass`
  - shadow tokens: `shadow`, `shadowcaster`, `castshadow`
- Added depth-only blend state (`blend_depth_only`):
  - color write mask `0`
  - depth write enabled
- Render queue/pipeline now executes pass sequence:
  1. `DepthOnly`
  2. `ShadowCaster`
  3. `Base` (opaque/mask + blend base path)
  4. `Outline`
  5. `Emission` (additive)
  6. `Blend`
- Existing `FastFallback` policy disables additional high-cost passes (`depth/shadow/outline/emission`) and keeps conservative base rendering.
- Runtime pass counters were expanded and surfaced into `last_render_pass_summary`:
  - `depth/shadow/base/outline/emission/blend`

### 4) Parity fallback reason enrichment

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Parity diagnostics now emit missing-pass hints when relevant:
  - `missing_depth_pass`
  - `missing_shadow_pass`

## Verification

Executed:

```powershell
cmake --build NativeAnimiq/build --config Release --target nativecore avatar_tool
dotnet build NativeAnimiq/host/HostCore/HostCore.csproj -c Release
NativeAnimiq/build/Release/avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.miq"
```

Observed:

- `nativecore/avatar_tool` build: PASS
- `HostCore` build: PASS
- sample MIQ load: PASS
  - `Format: MIQ`
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`

## Follow-ups

- `ShadowCaster` pass currently establishes topology/state separation; shadow map generation/sampling fidelity is a follow-up slice.
- Variant matrix selection (`shader_variant + keyword_set -> deterministic variant table`) should be the next parity-hardening step.
- Snapshot parity scoring (`SSIM/DeltaE`) should be wired to convert `parity_score` into measurable gate thresholds.
