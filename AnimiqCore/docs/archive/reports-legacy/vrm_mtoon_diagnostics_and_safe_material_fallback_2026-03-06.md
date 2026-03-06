# VRM MToon Diagnostics Precision + Safe Material Fallback (2026-03-06)

## Summary

This pass hardens VRM runtime behavior in three places:

1. removes false-positive `MToon matcap` missing-feature reporting,
2. adds renderer-side safe fallback for unresolved VRM texture references in material slots,
3. improves UI-facing warning selection so render/material problems are surfaced first.

The focus is reliability and diagnosability under mixed-quality VRM assets, not full shader-parity expansion.

## Scope

- In:
  - `src/avatar/vrm_loader.cpp`
  - `src/nativecore/native_core.cpp`
  - reporting docs/index updates
- Out:
  - full MToon feature parity matrix implementation
  - wire-format/schema changes
  - Unity exporter contract changes

## Implementation Details

### 1) VRM loader matcap diagnostics policy refinement

File: `src/avatar/vrm_loader.cpp`

- Added `MaterialInfo::matcap_declared`.
- Marked `matcap_declared=true` when VRM MToon declares:
  - `matcapFactor`, or
  - `matcapTexture`.
- End-of-load diagnostics policy changed:
  - before:
    - if any MToon binding existed but no global matcap binding was detected, loader appended `MToon matcap` to `missing_features`.
  - after:
    - `MToon matcap` is not treated as a generic missing feature.
    - if matcap was declared but unresolved, emit warning/code:
      - `W_MTOON: VRM_MTOON_MATCAP_UNRESOLVED: materials=<n>`
      - `VRM_MTOON_MATCAP_UNRESOLVED`

Effect:
- assets that simply do not use matcap are no longer misclassified as missing matcap capability.

### 2) Native renderer VRM safe material fallback

File: `src/nativecore/native_core.cpp`

- During GPU material build, unresolved texture tracking was added for:
  - `base`
  - `normal`
  - `rim`
  - `emission`
  - `matcap`
  - `uvAnimationMask`
- For VRM source avatars only, unresolved slots trigger conservative fallback:
  - base unresolved:
    - disable base texture SRV
    - clamp base color upward to avoid black-collapse appearance
  - normal/rim/emission/matcap unresolved:
    - disable corresponding SRV and runtime strengths
  - uv mask unresolved:
    - disable uv mask SRV and uv animation flag
- Fallback warning code:
  - `VRM_MATERIAL_SAFE_FALLBACK_APPLIED`
- Existing MIQ conservative/fallback behavior is preserved and still emits:
  - `MIQ_MATERIAL_FALLBACK_APPLIED`

Effect:
- runtime avoids unstable visual contributions from unresolved VRM texture bindings.

### 3) UI-facing warning prioritization

File: `src/nativecore/native_core.cpp`

- Warning classification expanded for:
  - `VRM_MATERIAL_SAFE_FALLBACK_APPLIED`
  - `VRM_MTOON_MATCAP_UNRESOLVED`
- Added preferred warning selectors for `FillAvatarInfo`:
  - prefer render/material-oriented warning codes for `last_warning_code`.
  - prefer render/material-oriented warning messages for `last_warning`.

Effect:
- diagnostics panel less likely to be dominated by non-render trailing warnings while material/render problems exist.

## Verification

## Build

- `cmake --build NativeAnimiq/build-thumb --config Release --target nativecore avatar_tool`
  - result: PASS

## Runtime probe

- `NativeAnimiq/build-thumb/Release/avatar_tool.exe sample/개인작11-3.vrm`
  - result: PASS
  - key observations:
    - `Format: VRM`
    - `MissingFeatures: 2`
    - `LastMissingFeature: MToon uv animation`
    - no generic `MToon matcap` missing-feature report
- `NativeAnimiq/build-thumb/Release/avatar_tool.exe sample/Kikyo_FT Variant.vrm`
  - result: PASS
  - key observations:
    - no regression to generic `MToon matcap` missing-feature behavior

## Notes / Remaining Risk

- This pass improves resilience and diagnostics clarity but does not claim complete MToon keyword-level parity.
- If visual mismatch persists on specific assets, next step is per-material typed payload diffing between loader extraction and renderer consumption.
