# VRM MToon Gate Hardening + Stage1 Baseline (2026-03-06)

## Summary

Implemented stage-1 MToon parity operations hardening:

- VRM unresolved material warning code normalization,
- `avatar_tool` MToon coverage/fallback counter expansion,
- VRM quality gate extension with `GateK`/`GateL`.

## Changed

- `src/nativecore/native_core.cpp`
  - VRM unresolved textures now emit `VRM_MATERIAL_TEXTURE_UNRESOLVED`.
  - VRM MToon/material warning classification synced for render-category diagnostics.
- `tools/avatar_tool.cpp`
  - added:
    - `MtoonOutlineMaterials`
    - `MtoonUvAnimMaterials`
    - `MtoonMatcapMaterials`
    - `VrmSafeFallbackWarnings`
    - `VrmMatcapUnresolvedWarnings`
    - `VrmTextureUnresolvedWarnings`
- `tools/vrm_quality_gate.ps1`
  - added GateK (strict unresolved/fallback fail).
  - added GateL (advanced-feature coverage mode observation).
  - per-sample lines now include MToon coverage and VRM warning vectors.

## Verification

- `cmake --build NativeVsfClone/build-thumb --config Release --target nativecore avatar_tool`: PASS
- `avatar_tool sample/개인작11-3.vrm`: PASS (new MToon/VRM counters present)
- `vrm_quality_gate.ps1 -Profile fixed5`: PASS
  - `GateK: PASS`
  - `GateL: PASS [mode=no-advanced-feature-coverage]`
