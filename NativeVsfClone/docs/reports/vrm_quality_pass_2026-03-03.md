# VRM Quality Pass Report (2026-03-03)

## Summary

This pass starts the VRM quality sprint with two deliverables:

- material/texture payload extraction improvements in `vrm_loader`
- strict 5-sample VRM gate harness (`tools/vrm_quality_gate.ps1`)

## Scope

- parse glTF `materials`, `textures`, `images` from VRM(GLB)
- extract image bytes from BIN `bufferView`
- populate `material_payloads` and `texture_payloads`
- downgrade compatibility to `partial` on material/texture quality issues
- add strict gate checks for 5-sample stabilization

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
  - selects 5 `.vrm` samples (sorted) by default
  - validates:
    - Gate A: load stability
    - Gate B: `Format=VRM`, `ParserStage=runtime-ready`, `Compat!=failed`, `MeshPayloads>0`
    - Gate C: `MaterialPayloads>0`, `TexturePayloads>0`
  - outputs:
    - `build/reports/vrm_probe_latest.txt`
    - `build/reports/vrm_gate_summary.txt`

## Verification

- Build:
  - `cmake --build build --config Release --target avatar_tool`
- Gate:
  - `powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1`

Result:

- Gate A: PASS
- Gate B: PASS
- Gate C: PASS
- Overall: PASS

Validated sample set (auto-sorted first 5 under `sample/`):

- `Kikyo_FT Variant(Clone).vrm`
- `Kikyo_FT Variant.vrm`
- `Kikyo_FT Variant111.vrm`
- `Kikyo_FT Variant112.vrm`
- `MANUKA_FT Varian 개인작05.vrm`

Representative counters from gate summary:

- mesh payload range: `21..27`
- material payload range: `13..18`
- texture payload range: `8..22`

Generated outputs:

- `build/reports/vrm_probe_latest.txt`
- `build/reports/vrm_gate_summary.txt`
