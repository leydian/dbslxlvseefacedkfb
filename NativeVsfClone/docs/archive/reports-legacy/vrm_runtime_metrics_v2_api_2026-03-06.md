# VRM Runtime Metrics v2 API + Spring/MToon Quality Instrumentation (2026-03-06)

## Summary

This pass adds a new runtime-facing diagnostics contract (`v2`) for practical quality tracking of SpringBone simulation behavior and MToon advanced runtime application.

The goal is to provide measurable runtime signals that can drive quality gates, host diagnostics, and future optimization work without breaking existing `NcAvatarInfo`/`NcSpringBoneInfo` consumers.

## Scope

In scope:

- native API extension (`NcAvatarRuntimeMetricsV2` + getter)
- native runtime metric accumulation (Spring solver + MToon runtime usage)
- HostCore interop sync for immediate managed consumption
- build/gate validation

Out of scope:

- replacing existing API surfaces
- introducing API migration/deprecation policy changes
- full visual regression gate implementation

## Implemented Changes

### 1) New native diagnostics contract (v2)

- File: `include/vsfclone/nativecore/api.h`
- Added `NcAvatarRuntimeMetricsV2`:
  - Spring runtime quality metrics:
    - `spring_active_chain_count`
    - `spring_constraint_hit_count`
    - `spring_damping_event_count`
    - `spring_avg_offset_magnitude`
    - `spring_peak_offset_magnitude`
  - MToon runtime usage metrics:
    - `mtoon_outline_material_count`
    - `mtoon_uv_anim_material_count`
    - `mtoon_matcap_material_count`
    - `mtoon_blend_material_count`
    - `mtoon_mask_material_count`
  - Frame budget/identity fields:
    - `last_frame_ms`
    - `target_frame_ms`
    - `physics_solver`
    - `mtoon_runtime_mode`
- Added API function:
  - `nc_get_avatar_runtime_metrics_v2(NcAvatarHandle, NcAvatarRuntimeMetricsV2*)`

### 2) Native runtime metric accumulation/fill

- File: `src/nativecore/native_core.cpp`
- Extended per-avatar secondary motion state with frame metrics:
  - `constraint_hit_count`
  - `damping_event_count`
  - `avg_offset_magnitude`
  - `peak_offset_magnitude`
- Updated Spring runtime loop instrumentation:
  - increments constraint-hit counter when radial constraint clamps chain displacement
  - increments damping-event counter for unsupported collider damping path
  - computes per-frame average/peak chain offset magnitude
- Added `FillAvatarRuntimeMetricsV2(...)`:
  - pulls Spring metrics from runtime state
  - computes MToon runtime counts from GPU material resources when available
  - falls back to material diagnostics when runtime GPU resources are absent
  - stamps solver/mode identifiers and 60fps target frame budget
- Added exported API implementation:
  - `nc_get_avatar_runtime_metrics_v2(...)`

### 3) Host interop synchronization

- File: `host/HostCore/NativeCoreInterop.cs`
- Added C# struct mirror for `NcAvatarRuntimeMetricsV2`
- Added DllImport declaration for `nc_get_avatar_runtime_metrics_v2`

## Verification

Executed on 2026-03-06:

- `cmake --build .\build --config Release --target nativecore avatar_tool` : PASS
- `dotnet build .\host\HostCore\HostCore.csproj -c Release` : PASS
- `powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5` : PASS (GateA..GateJ)

## Notes

- Existing API contracts remain intact; this is additive v2 diagnostics.
- The new v2 metrics unlock quantitative acceptance criteria for the next Spring/MToon optimization steps.
