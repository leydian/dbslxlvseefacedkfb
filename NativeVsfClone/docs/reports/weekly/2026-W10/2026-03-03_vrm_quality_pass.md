# VRM Quality Pass Report (2026-03-03)

## Summary

This pass starts the VRM quality sprint with two deliverables:

- material/texture payload extraction improvements in `vrm_loader`
- strict 5-sample VRM gate harness (`tools/vrm_quality_gate.ps1`)
- expression extraction/runtime mapping visibility

## Scope

- parse glTF `materials`, `textures`, `images` from VRM(GLB)
- extract image bytes from BIN `bufferView`
- populate `material_payloads` and `texture_payloads`
- downgrade compatibility to `partial` on material/texture quality issues
- add strict gate checks for 5-sample stabilization
- expose expression/render diagnostics through NativeCore/API

## Changes

- `src/avatar/vrm_loader.cpp`
  - added:
    - `TryGetBool(...)`
    - `DetectTextureFormat(...)`
    - `ReadBufferViewBytes(...)`
  - material parse coverage:
    - `name`, `doubleSided`, `alphaMode`, `alphaCutoff`
    - `pbrMetallicRoughness.baseColorTexture`
  - image extraction:
    - reads image bytes via `images[].bufferView`
  - quality diagnostics:
    - `VRM_TEXTURE_MISSING`
    - `VRM_MATERIAL_UNSUPPORTED`
  - compatibility policy:
    - mesh path succeeds + material/texture issues => `Compat=partial`

- `tools/vrm_quality_gate.ps1` (new)
  - supports profile split:
    - `fixed5` (default)
    - `auto5` (sorted discovery)
  - validates:
    - Gate A: load stability
    - Gate B: `Format=VRM`, `ParserStage=runtime-ready`, `Compat!=failed`, `MeshPayloads>0`
    - Gate C: `MaterialPayloads>0`, `TexturePayloads>0`
    - Gate D: `ExpressionCount>0`
  - outputs:
    - `build/reports/vrm_probe_fixed5.txt`
    - `build/reports/vrm_gate_fixed5.txt`
    - `build/reports/vrm_probe_auto5.txt`
    - `build/reports/vrm_gate_auto5.txt`

- `src/nativecore/native_core.cpp` + `tools/avatar_tool.cpp`
  - runtime expression mapping and diagnostics surfaced:
    - `ExpressionCount`
    - `LastRenderDrawCalls`
    - `LastExpressionSummary`

## Verification

- Build:
  - `cmake --build build --config Release --target avatar_tool`
- Gate:
  - `powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5`
  - `powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile auto5`

Result:

- Gate A: PASS
- Gate B: PASS
- Gate C: PASS
- Gate D: PASS
- Overall: PASS

Validated `fixed5` sample set:

- `Kikyo_FT Variant(Clone).vrm`
- `Kikyo_FT Variant.vrm`
- `Kikyo_FT Variant111.vrm`
- `Kikyo_FT Variant112.vrm`
- `NewOnYou.vrm`

Validated `auto5` sample set:

- `Kikyo_FT Variant(Clone).vrm`
- `Kikyo_FT Variant.vrm`
- `Kikyo_FT Variant111.vrm`
- `Kikyo_FT Variant112.vrm`
- `MANUKA_FT Varian*.vrm`

Representative counters from gate summary:

- mesh payload range: `21..27`
- material payload range: `13..18`
- texture payload range: `8..22`

Generated outputs:

- `build/reports/vrm_probe_fixed5.txt`
- `build/reports/vrm_gate_fixed5.txt`
- `build/reports/vrm_probe_auto5.txt`
- `build/reports/vrm_gate_auto5.txt`
