# NativeCore Render Quality API Sync (2026-03-02)

## Summary

Synchronized the public native C API header with already-implemented render quality controls in `native_core.cpp`.

This closes the gap where runtime behavior existed internally, but the ABI contract in `include/vsfclone/nativecore/api.h` did not expose it.

## Implemented Changes

1. Added public render quality types to C API header
- File: `include/vsfclone/nativecore/api.h`
- Added:
  - `NcCameraMode`
    - `NC_CAMERA_MODE_AUTO_FIT_FULL`
    - `NC_CAMERA_MODE_AUTO_FIT_BUST`
    - `NC_CAMERA_MODE_MANUAL`
  - `NcRenderQualityOptions`
    - `camera_mode`
    - `framing_target`
    - `headroom`
    - `yaw_deg`
    - `fov_deg`
    - `background_rgba[4]`
    - `show_debug_overlay`

2. Added public function declarations
- `nc_set_render_quality_options(const NcRenderQualityOptions* options)`
- `nc_get_render_quality_options(NcRenderQualityOptions* out_options)`

## Why this matters

- Host and external integrations can now configure render framing/background/debug options through the official ABI.
- Header/runtime mismatch risk is removed for these controls.
- Future HostCore interop updates can bind these APIs without private/native-side assumptions.

## Compatibility

- Backward-compatible additive API change.
- Existing integrations remain valid; no enum/struct removals or field reordering in existing public types.

## Validation Notes

- `native_core.cpp` already contained the backing runtime state and helper logic for render quality option handling.
- This update makes the ABI declaration explicit and consumable by clients.
