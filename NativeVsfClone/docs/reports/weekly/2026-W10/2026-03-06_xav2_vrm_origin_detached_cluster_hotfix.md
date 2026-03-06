# XAV2 VRM-origin detached-cluster hotfix + bust focus retune (2026-03-06)

## Scope

- This report covers a follow-up runtime hotfix for the case where `sourceExt=.vrm` XAV2 avatars still showed:
  - detached floating head/hair clusters
  - bust framing that looked sunken even after prior outlier/autofit hardening
- Out of scope:
  - exporter payload/schema changes
  - host UI flow updates
  - non-XAV2 rendering paths

## Implemented Changes

- Primary code path:
  - `src/nativecore/native_core.cpp`

- Static skinning policy rollback for VRM-origin XAV2:
  - `ShouldApplyStaticSkinningForAvatarMeshes(...)` restored VRM-origin default to bind-pose path.
  - reason: avoid mixed per-mesh static-skinning outcomes causing face/hair/body desync.

- VRM-origin draw guard hardening:
  - in draw queue build, if avatar is XAV2 with `sourceExt=.vrm`:
    - meshes excluded by preview-bounds filter are also skipped in draw path.
  - added dedicated detached-cluster skip branch for VRM-origin:
    - `robust_dist > max(1.4, median_center_dist * 2.8)`
    - `emax <= max(1.8, median_extent * 3.2)`
  - new warning code:
    - `XAV2_VRM_ORIGIN_DETACHED_CLUSTER_SKIPPED`

- VRM-origin bust focus retune:
  - under `AutoFitBust`, XAV2+VRM-origin adds a stronger robust-center pull:
    - blended with `vrm_focus_from_cluster = robust_cy + safe_extent_y * 0.06`
    - blend: `focus_y = focus_y * 0.25 + vrm_focus_from_cluster * 0.75`
  - additional VRM-origin clamp window:
    - `[avatar_bmin.y + 0.48 * extent_y, avatar_bmin.y + 0.76 * extent_y]`

- Diagnostics update:
  - preview debug string now appends:
    - `vrm_origin_detached_skipped=<count>`

- Contract/docs sync:
  - `docs/formats/xav2.md`
    - re-aligned VRM-origin static skinning statement with runtime rollback
    - added warning contract entry for `XAV2_VRM_ORIGIN_DETACHED_CLUSTER_SKIPPED`

## Verification Summary

- Build:
  - `cmake --build NativeVsfClone/build --config Release --target nativecore` -> PASS

- Runtime deployment:
  - replaced `dist/wpf/nativecore.dll` with latest build output after stopping `WpfHost.exe`
  - SHA256 parity confirmed:
    - `build/Release/nativecore.dll` == `dist/wpf/nativecore.dll`

- Loader smoke:
  - `NativeVsfClone/build/Release/avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작10-2.xav2" --dump-warnings-limit=50` -> PASS

## Known Risks or Limitations

- VRM-origin detached-cluster thresholds are heuristic; false skips on unusual accessory rigs remain possible.
- `avatar_tool` does not validate final on-screen composition; visual confirmation still requires host runtime.
- This hotfix intentionally biases stability over maximum accessory retention for VRM-origin XAV2.

## Next Steps

1. Run the target avatar through host runtime and check warning list for `XAV2_VRM_ORIGIN_DETACHED_CLUSTER_SKIPPED`.
2. If detached skips are excessive, tune VRM-origin size cap first (`max(1.8, median_extent * 3.2)`).
3. Add targeted regression samples to `xav2_render_regression_gate` corpus for sustained guard coverage.
