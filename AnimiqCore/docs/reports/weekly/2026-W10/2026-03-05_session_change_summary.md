# Session Change Summary (2026-03-05)

## Summary

This report consolidates the latest change line after the WPF crash hotfix, including Unity MIQ export updates, native UV/material/render-orientation corrections, avatar extension policy trim, and current load-failure behavior observed in host runtime.

Primary outcomes:

1. WPF crash-path hardening was applied and verified (`cross-thread UI access` guard).
2. Unity MIQ SDK gained an opt-in relaxed export menu path for shader-policy bypass scenarios.
3. Unity MIQ texture extraction path was hardened for lilToon/non-readable texture cases via PNG fallback capture.
4. Native MIQ render path fixed UV offset decoding for expanded-stride vertex payloads.
5. MIQ default preview orientation was corrected to front-view in host runtime and material mapping reliability was hardened.
6. Host/runtime avatar extension policy was trimmed to `.vrm`, `.vsfavatar`, `.miq` in this branch line.
7. Runtime load failures surfaced as `Load failed: Unsupported` remain possible when native render-resource requirements are not satisfied.

## Change Set Rollup

### 1) WPF crash hotfix track

- reference commit: `396f267`
- key changes:
  - `host/WpfHost/MainWindow.xaml.cs`
    - added UI-thread marshal helper and routed error/progress UI updates through dispatcher-safe path.
  - reporting updates:
    - `docs/reports/host_blocker_status_board_2026-03-05.md`
    - `docs/reports/host_blocker_closure_implementation_pass_2026-03-05.md`
- verification snapshot:
  - HostCore/WPF build PASS
  - `tools/publish_hosts.ps1` PASS (`WPF_ONLY`)
  - `build/reports/wpf_launch_smoke_latest.txt` PASS

### 2) Unity MIQ relaxed export menu track

- reference commit: `f1d013b`
- key changes:
  - `unity/Packages/com.animiq.miq/Editor/MiqExportMenu.cs`
    - added relaxed export entry for opt-in strict shader-policy bypass path.
  - docs:
    - `docs/reports/miq_relaxed_export_menu_2026-03-05.md`
    - `docs/INDEX.md` link update

### 3) Avatar extension policy trim track

- reference commit: `dc33918`
- key changes:
  - removed loader/runtime support path for `.vxavatar` / `.vxa2` from current host-facing flow.
  - extension validation and guidance narrowed to `.vrm`, `.vsfavatar`, `.miq`.
  - removed sources:
    - `src/avatar/vxavatar_loader.*`
    - `src/avatar/vxa2_loader.*`
  - updated docs:
    - `docs/reports/avatar_extension_policy_trim_2026-03-05.md`
    - `README.md` and host guidance strings

### 4) Unity MIQ lilToon texture fallback track

- reference commit: `358c2ae`
- key changes:
  - `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`
    - texture export fallback added (`EncodeToPNG` fail -> RenderTexture/ReadPixels path).
    - base texture probe expanded (`_MainTex`, `_BaseMap`, `_BaseColorMap`).
    - warning log added for persistent texture encode failure.
  - docs:
    - `docs/reports/miq_liltoon_texture_export_fallback_2026-03-05.md`
    - `docs/INDEX.md` link update

### 5) Native MIQ UV offset decode fix track

- reference commit: `2bf5fc6`
- key changes:
  - `src/nativecore/native_core.cpp`
    - corrected UV extraction offset for expanded-stride vertex payloads (MIQ Unity layout).
    - retained compatibility with compact legacy payloads through stride-aware offset branching.
  - docs:
    - `docs/reports/miq_native_uv_offset_fix_2026-03-05.md`
    - `docs/INDEX.md` link update

### 6) MIQ front-view + material alignment track

- reference commit: pending (current workspace update)
- key changes:
  - `src/nativecore/native_core.cpp`
    - MIQ preview world rotation adjusted for front-view default.
    - material base color/alpha inference expanded from `shader_params_json`.
  - `src/avatar/miq_loader.cpp`
    - removed unsafe first-texture fallback when material base texture is empty.
  - `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`
    - stable material/texture key emission and alpha-mode inference hardening.
  - docs:
    - `docs/reports/miq_front_view_and_material_alignment_2026-03-05.md`
    - `docs/INDEX.md` link update

## Current Runtime Behavior Notes

### Why `Load failed: Unsupported` can still appear

- this message is returned when native runtime reports `NcResultCode.Unsupported`.
- one known native path is render-resource creation failure when avatar payload has no renderable mesh payloads.
- in that case the failure is runtime capability/payload-state related, not necessarily a file-extension validation error.

### Operational interpretation

- `Unsupported avatar file extension` (validation message):
  - happens before native load call if extension is outside current policy.
- `Load failed: Unsupported` (runtime message):
  - happens after native load call path and indicates native/runtime unsupported condition.

## Files Most Relevant for Follow-up

- `host/HostCore/HostController.cs`
- `host/HostCore/HostController.MvpFeatures.cs`
- `host/WpfHost/MainWindow.xaml.cs`
- `src/nativecore/native_core.cpp`
- `docs/reports/avatar_extension_policy_trim_2026-03-05.md`

## Outstanding Follow-up

1. If extension policy should include `.vxavatar` / `.vxa2` again, policy/loader/docs must be reverted as a single contract change.
2. Improve operator diagnostics for `Load failed: Unsupported` by surfacing `nc_get_last_error` details directly in host dialog/log path.
3. Re-run manual `.vsfavatar` failure reproduction loop and capture error-detail evidence in report artifacts.
