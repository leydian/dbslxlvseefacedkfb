# VRM Runtime Slice Update (2026-03-03)

## Summary

This update delivers the first executable VRM path that reaches runtime-ready payload generation and minimal native render execution.

Scope covered:

- `.vrm` GLB parse and mesh payload extraction
- NativeCore render lifecycle validation with minimal D3D11 frame operation

## Implemented Changes

1. VRM loader upgrade (`src/avatar/vrm_loader.cpp`)
- Replaced scaffold-only behavior with minimal GLB v2 parsing:
  - validates GLB magic/version/length
  - extracts JSON and BIN chunks
- Added lightweight in-file JSON parser for required glTF structures.
- Added mesh extraction path:
  - reads `POSITION` accessor (`FLOAT VEC3`)
  - reads optional indices (`UNSIGNED_BYTE/SHORT/INT`)
  - falls back to sequential indices when missing
- Populates `AvatarPackage` payload fields:
  - `mesh_payloads`
  - `materials` / `material_payloads` (minimal placeholder material)
- Added staged parser diagnostics:
  - `parse -> resolve -> payload -> runtime-ready`
  - `VRM_SCHEMA_INVALID`, `VRM_ASSET_MISSING`, `NONE`

2. Native render path upgrade (`src/nativecore/native_core.cpp`)
- `nc_create_render_resources` now requires renderable mesh payloads.
- `nc_render_frame` now performs a minimal D3D11 action:
  - bind current RTV
  - clear render target view
- Kept existing argument validation contract for D3D11 pointers and context size.

3. Build integration (`CMakeLists.txt`)
- Linked `nativecore` with `d3d11` on Windows.

## Validation

Build:

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
- `cmake --build build --config Release`

Runtime checks (`avatar_tool`):

1. `sample/개인작08.vrm`
- `Format: VRM`
- `Compat: full`
- `ParserStage: runtime-ready`
- `MeshPayloads: 9`

2. `sample/Kikyo_FT Variant.vrm`
- `Format: VRM`
- `Compat: full`
- `ParserStage: runtime-ready`
- `MeshPayloads: 22`

## Remaining Gaps

- Full MToon parameter mapping/binding
- SpringBone and expression support
- Full GUI host integration (WinUI/WPF)
- Real draw path beyond minimal RTV clear and lifecycle checks
