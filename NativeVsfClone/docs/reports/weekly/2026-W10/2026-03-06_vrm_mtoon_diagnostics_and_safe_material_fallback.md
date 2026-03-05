# VRM MToon Diagnostics Precision + Safe Material Fallback (2026-03-06)

## Summary

Implemented a VRM reliability hardening pass that:

- removes false-positive `MToon matcap` missing-feature reporting,
- adds safe renderer fallback for unresolved VRM material texture slots,
- prioritizes render/material warnings in `last_warning*` diagnostics output.

## Changed

- `src/avatar/vrm_loader.cpp`
  - added `matcap_declared` tracking per material.
  - changed matcap policy:
    - no longer appends `MToon matcap` to `missing_features` when matcap is simply unused.
    - emits `VRM_MTOON_MATCAP_UNRESOLVED` only when matcap is declared but unresolved.
- `src/nativecore/native_core.cpp`
  - added unresolved texture tracking for `base/normal/rim/emission/matcap/uvAnimationMask`.
  - VRM-only safe fallback now disables unstable slot contributions and applies conservative defaults.
  - added warning code `VRM_MATERIAL_SAFE_FALLBACK_APPLIED`.
  - warning classifier + `FillAvatarInfo` selection now prefer render/material warnings for UI-facing `last_warning` surfaces.

## Verification

- `cmake --build NativeVsfClone/build-thumb --config Release --target nativecore avatar_tool`: PASS
- `avatar_tool sample/개인작11-3.vrm`: PASS
  - `LastMissingFeature: MToon uv animation`
  - no generic `MToon matcap` missing-feature output
- `avatar_tool sample/Kikyo_FT Variant.vrm`: PASS
  - no regression in matcap missing-feature behavior
