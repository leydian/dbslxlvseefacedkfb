# 2026-03-06 Avatar Preview Flip180 Auto Contract (VRM + MIQ VRM-origin)

## Summary

Implemented load-time automatic `PreviewFlip180` resolution so VRM and VRM-origin MIQ use loader/runtime contract yaw as primary truth, with runtime bounds/hair validation as a safety gate before persistence override.

Primary outcomes:

- host no longer blindly replays stale per-avatar `PreviewFlip180` on load,
- load path now resolves desired flip from native runtime metrics (`contract yaw + confidence + validation`),
- auto decision persists back into `RecentAvatars.PreviewFlip180` and can override previous stored value,
- runtime metrics v2 now exposes contract/validation fields needed by host without parsing diagnostic strings.

## Changed

- Native C API metrics extension:
  - `include/animiq/nativecore/api.h`
  - `NcAvatarRuntimeMetricsV2` added:
    - `contract_preview_yaw_deg`
    - `transform_confidence_level`
    - `is_vrm_origin_miq`
    - `preview_bounds_excluded_mesh_count`
    - `preview_hair_candidate_mesh_count`
    - `preview_hair_head_alignment_score`
- Native runtime metrics population:
  - `src/nativecore/native_core.cpp`
  - added per-avatar orientation metric cache (`avatar_preview_orientation_metrics`)
  - `FillAvatarRuntimeMetricsV2(...)` now returns contract yaw/confidence and validation signals
  - render pass now computes/stores hair-head alignment score for VRM-origin MIQ candidates
  - cache lifecycle cleanup wired into load/unload/create/destroy paths
- Host interop:
  - `host/HostCore/NativeCoreInterop.cs`
  - managed `NcAvatarRuntimeMetricsV2` updated to mirror native struct fields
- Host load behavior:
  - `host/HostCore/HostController.cs`
  - `LoadAvatar(...)` now calls `ResolveAndApplyAvatarPreviewFlipOnLoad(path)` instead of stored-only replay
  - new resolver policy:
    - scope: `VRM`, `MIQ + is_vrm_origin_miq`
    - decision source: `contract_preview_yaw_deg` (`abs(yaw) >= 90 -> flip`)
    - acceptance gate: `transform_confidence_level >= medium` OR (`bounds/hair` validation stable)
    - persistence: auto result writes back through `SetAvatarPreviewFlip180Preference(...)`
    - telemetry: `AvatarPreviewFlipResolve` host log line includes decision inputs/result

## Decision Rule

1. Read existing stored `PreviewFlip180` for path.
2. Query `nc_get_avatar_runtime_metrics_v2(activeHandle, ...)`.
3. If avatar is in scope (`VRM` or `MIQ VRM-origin`):
   - derive `contractFlip` from `contract_preview_yaw_deg`,
   - compute validation health from:
     - `preview_bounds_excluded_mesh_count`
     - `preview_hair_candidate_mesh_count`
     - `preview_hair_head_alignment_score`,
   - when confidence/validation gate passes, apply contract result and persist it.
4. Apply `+180` yaw only if final resolved flip is `true`.

## Verification

- `cmake --build AnimiqCore/build_plan_impl --config Release --target nativecore`: PASS
- `dotnet build AnimiqCore/host/HostCore/HostCore.csproj -c Release --no-restore`: PASS
- `dotnet build AnimiqCore/host/WpfHost/WpfHost.csproj -c Release --no-restore`: PASS

