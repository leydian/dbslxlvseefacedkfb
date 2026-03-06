# 2026-03-06 - Arm pose policy alignment fix (restore arm angle movement, including MIQ)

## Summary
- Resolved a runtime regression where arm angle controls updated host state but arm bones did not move visually.
- Root cause was a policy mismatch between:
  - mesh static-skinning enable decision (`auto/on/off` aware), and
  - arm pose apply gate (force-on env only).
- Updated arm pose gate to use the same per-avatar static-skinning policy and removed MIQ forced skip.

## Symptoms Observed
- WPF arm sliders (`Both/Left/Right`) changed numeric values correctly.
- `HostController.SetPoseOffset(...)` was invoked and `nc_set_pose_offsets(...)` payload was updated.
- In native runtime, `ApplyArmPoseToAvatar(...)` returned early under default/auto settings, resulting in no visible arm angle change.

## Root Cause
1. `ApplyArmPoseToAvatar(...)` was guarded by `ShouldApplyExperimentalStaticSkinning()`.
2. That function only returned true for explicit force-on env (`ANIMIQ_MIQ_ENABLE_STATIC_SKINNING=1|true|yes|on`).
3. Under default/auto runtime, arm pose apply path was skipped even when mesh static skinning was active.
4. Additional MIQ-only early return unconditionally disabled arm pose for MIQ avatars.

## Implementation Changes
- File: `src/nativecore/native_core.cpp`
- Function: `ApplyArmPoseToAvatar(...)`

### 1) Gate policy alignment
- Before:
  - `if (!ShouldApplyExperimentalStaticSkinning()) return true;`
- After:
  - `if (!ShouldApplyStaticSkinningForAvatarMeshes(avatar_pkg)) return true;`
- Impact:
  - arm pose now follows the same policy resolution as mesh build static skinning.
  - default/auto behavior no longer silently disables arm pose where mesh policy is active.

### 2) MIQ forced skip removal
- Removed temporary MIQ early return block.
- Impact:
  - MIQ arm pose can now apply under enabled policy and valid rig/skeleton payload prerequisites.

### 3) Skip reason warning contract
- Added warning emission when arm pose is skipped by static-skinning policy:
  - code: `ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY`
  - message:
    - `W_RENDER: ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY: arm pose skipped due to static skinning policy.`
- Emitted through `PushAvatarWarningUnique(...)` to avoid duplicate spam.

## Compatibility and Safety
- No public API signature changes.
- Existing static-skinning policy env semantics remain unchanged:
  - `on/true/1`: enabled
  - `off/false/0`: disabled
  - unset/auto: avatar-policy auto resolution
- Existing collapse/extent safety guards remain active in posed mesh path.

## Verification
- Build:
  - `cmake --build NativeAnimiq/build --config Release --target nativecore` -> PASS
- Static checks:
  - confirmed `ApplyArmPoseToAvatar(...)` now references `ShouldApplyStaticSkinningForAvatarMeshes(avatar_pkg)`.
  - confirmed MIQ early-return bypass removal in arm pose function.
  - confirmed warning code insertion path for policy-disabled case.

## Expected Runtime Behavior After Fix
- Arm sliders should produce visible upper-arm pose updates in VRM and MIQ when policy is enabled/auto-resolved ON.
- If policy is disabled (`force-off`), arm pose remains skipped and emits explicit warning code for diagnostics.
