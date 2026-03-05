# VRM Texture-Alpha Fallback Recovery + WPF Redeploy (2026-03-06)

## Summary

This update fixes the visible "broken model" symptom observed on `NewOnYou.vrm` in WPF runtime where meshes loaded successfully but major materials rendered with incorrect opacity behavior.

Root cause was not publish/runtime DLL mismatch this time. The model itself omitted alpha metadata that our VRM alpha resolver depended on:

- glTF `materials[*].alphaMode`: missing
- VRM0 `extensions.VRM.materialProperties[*]` alpha hints (`renderMode`, `_ALPHABLEND_ON`, `_ALPHATEST_ON`, `_Cutoff`): missing

Because fallback behavior defaulted to opaque, all materials collapsed into `OPAQUE` even when base color textures were alpha-capable PNG assets.

This pass adds a deterministic texture-based alpha fallback in VRM loader and republishes WPF host.

## Symptom and Evidence

Observed from host diagnostics and `avatar_tool` for `D:\dbslxlvseefacedkfb\sample\NewOnYou.vrm`:

- `ParserStage: runtime-ready`
- `PrimaryError: NONE`
- `MaterialDiagnostics: 12`
- `OpaqueMaterials: 12`
- `BlendMaterials: 0`
- `LastMaterialDiag ... alphaMode=OPAQUE, alphaSource=default.opaque`

Interpretation:

- Parsing/payload path was functioning.
- Render breakage came from alpha classification policy, not load failure.

## Implementation Details

Updated file:

- `src/avatar/vrm_loader.cpp`

### 1) PNG alpha capability detection

Added helper path to infer alpha capability from texture bytes when metadata is absent:

- `ReadU32Be(...)`
- `BytesStartWith(...)`
- `IsPngWithAlphaCapability(...)`
- `TextureHasAlphaCapability(...)`

Detection rules:

- PNG color type `4` (gray+alpha) or `6` (RGBA) => alpha-capable
- PNG with `tRNS` chunk => alpha-capable
- currently scoped to PNG; non-PNG remains conservative

### 2) Image/material data wiring

`TextureRef` now tracks:

- `alpha_capable`

During image extraction:

- `image_table[i].alpha_capable = TextureHasAlphaCapability(...)`

`MaterialInfo` now tracks:

- `base_color_texture_alpha_capable`

When resolving `pbrMetallicRoughness.baseColorTexture`, loader copies texture alpha capability into material info.

### 3) Alpha fallback policy

After normal alpha hint extraction (`gltf.alphaMode`, extensions/materialProperties hints), a fallback is applied:

- if `alpha_mode == OPAQUE`
- and `alpha_source == default.opaque`
- and `base_color_texture_alpha_capable == true`

then:

- set `alpha_mode = BLEND`
- set `alpha_source = fallback.texture-alpha`

This preserves existing explicit hints and only changes the metadata-empty case.

## Verification

### Build

Executed:

- `cmake --build .\build --config Release --target nativecore avatar_tool` (pass)

### CLI validation (`avatar_tool`)

Command:

- `build\Release\avatar_tool.exe D:\dbslxlvseefacedkfb\sample\NewOnYou.vrm`

Before:

- `OpaqueMaterials: 12`
- `BlendMaterials: 0`
- `alphaSource=default.opaque`

After:

- `OpaqueMaterials: 6`
- `BlendMaterials: 6`
- `LastMaterialDiag ... alphaMode=BLEND, alphaSource=fallback.texture-alpha`

### WPF redeploy

Executed:

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1` (pass)

Outputs refreshed:

- `dist\wpf\WpfHost.exe`
- `dist\wpf\nativecore.dll`

## Operational Notes

- This fix addresses opacity classification for metadata-empty VRM materials.
- `SpringBone runtime simulation` remains a known independent gap and is unchanged in this pass.
- Fallback is intentionally constrained to avoid overriding explicit alpha contracts.
