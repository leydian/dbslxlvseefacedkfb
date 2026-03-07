# Spout Model-Only Output via Background Alpha Clear (2026-03-07)

## Scope

- Ensure Spout output excludes visible background and carries model-only composition intent.
- Keep existing host preview/background preset behavior unchanged when Spout is inactive.
- Out of scope: receiver-side alpha handling policy, new public API fields, or UI option expansion.

## Implemented Changes

### 1) Spout-active clear alpha policy in native render loop

- File: `src/nativecore/native_core.cpp`
- Function: `RenderFrameLocked(const NcRenderContext* ctx)`

Updated render-target clear alpha policy:

- When Spout is active (`g_state.spout.IsActive()`), clear color alpha is forced to `0.0f`.
- RGB clear values remain `quality.background_rgba[0..2]`.
- When Spout is not active, existing configured alpha (`quality.background_rgba[3]`) is preserved.

Implementation summary:

- Added `spout_active` boolean from runtime output state.
- Added `clear_alpha` computed as:
  - `0.0f` if Spout active
  - configured quality alpha otherwise
- Applied `clear_alpha` to render-target clear call input.

Result:

- Spout GPU path (`SubmitFrameTexture`) and legacy CPU fallback path (`CaptureRtvBgra` -> `SubmitFrame`) now both transmit frames with transparent background alpha while Spout is active.
- Existing frame routing/backends are unchanged.

## Compatibility and Behavioral Notes

- Public C ABI remains unchanged:
  - no struct shape changes (`NcRenderQualityOptions`, `NcSpoutOptions`, diagnostics structs unchanged)
  - no new API surface
- Host render/background presets remain unchanged.
- Transparency effect is backend-agnostic for Spout output because both backends share the same rendered source RT.

## Verification Summary

- Build verification attempted for native target:
  - `cmake --build . --config RelWithDebInfo --target animiq_core`
  - directory: `AnimiqCore/build_hotfix`
- Outcome: blocked by existing environment/build-state issue (`MSVC C1041` PDB lock on `animiq_core.pdb`), not by change-site compile diagnostics.

## Risks / Limitations

- Receivers that ignore alpha will still display RGB background content.
- Receiver-side premultiply/straight-alpha expectations are not modified in this patch.

