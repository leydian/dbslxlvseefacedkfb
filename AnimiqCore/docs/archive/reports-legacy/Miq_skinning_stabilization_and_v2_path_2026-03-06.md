# MIQ skinning stabilization and v2 path activation (2026-03-06)

## Summary

This update delivers the first implementation slice for the MIQ avatar rendering breakage case where skinned models exported with lilToon appeared collapsed/distorted in the native host.

The change set focuses on:

- native-side mesh stabilization using MIQ skin payloads
- minimal lilToon-oriented shading uplift for material readability
- MIQ version-path activation for v2 export/runtime flow

## Problem context

Observed behavior:

- MIQ file loaded successfully (`runtime-ready`, `compat=full`) but rendered avatar geometry was visibly collapsed.
- Source Unity view showed normal appearance for the same asset.

Root-cause line:

- native renderer consumed MIQ mesh/material payloads but did not apply skinning payload (`0x0013`) to vertex positions in render path.

## Implemented changes

### 1) Native render path: skinning stabilization

File:

- `src/nativecore/native_core.cpp`

Details:

- Added mesh-key normalization helper so mesh payload and skin payload resolve reliably even with case/path separator differences.
- Added skin weight decode routine for MIQ packed layout:
  - 4 x `int32` bone indices
  - 4 x `float` weights
  - 32 bytes per vertex
- Added static skinning application step (`ApplyStaticSkinningToVertexBlob`) executed during mesh upload build path:
  - input: mesh vertex blob + skin payload bindposes/weights
  - computes weighted transformed position and rewrites position channel before GPU buffer creation
- Wired skin payload lookup inside `EnsureAvatarGpuMeshes(...)` and passed matched skin payload to mesh builder.

Result:

- MIQ skinned meshes no longer depend on un-applied bind-space positions and render in stabilized form.

### 2) Native material path: lilToon-oriented phase-1 uplift

File:

- `src/nativecore/native_core.cpp`

Details:

- Extended GPU material runtime data with:
  - `shade_color`
  - `emission_color`
  - `shade_mix`
  - `emission_strength`
- Added parse path from `shader_params_json` for:
  - `_ShadeColor`
  - `_EmissionColor`
- Expanded shader constant buffer and pixel shader blend logic:
  - toon-like shade darkening mix
  - additive emission contribution
- Updated rasterizer selection to honor `double_sided` by switching between back-face cull and no-cull states.

Notes:

- This is a quality uplift, not full lilToon feature parity.
- Normal-map/rim/advanced keyword matrix/pass topology are still future work.

### 3) MIQ v2 path activation

Files:

- `src/avatar/miq_loader.cpp`
- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`

Details:

- Native loader version gate expanded from `v1 only` to `v1/v2`.
- Unity runtime loader version gate expanded from `v1 only` to `v1/v2`.
- Unity exporter now writes container `version=2`.

Compatibility intent:

- Existing v1 assets remain loadable in updated native/Unity runtime paths.
- New exports default to v2 for forward path alignment.

## Verification

Executed commands:

```powershell
cmake --build NativeAnimiq\build --config Release --target nativecore avatar_tool
NativeAnimiq\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.miq"
```

Observed:

- Build passed for `nativecore` and `avatar_tool`.
- MIQ sample parse remained healthy after changes:
  - `Load succeeded`
  - `Format: MIQ`
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`

## Follow-up scope

Planned next-quality layers (not part of this patch):

- full lilToon parity expansion (normal/rim/additional advanced properties and pass behavior)
- richer structured material parameter transport for high-fidelity shader reproduction
- image-based render regression baseline against Unity reference captures
