# 2026-03-06 - Arm pose MIQ(VRM-origin) auto-policy split fix

## Summary
- Implemented a native runtime hotfix to restore visible arm raise/lower motion for WPF arm sliders when using MIQ avatars exported from VRM.
- Split arm-pose apply policy from mesh static-skinning policy to avoid unintended arm-pose disablement in `auto` mode.
- Preserved existing mesh-safety behavior for VRM-derived MIQ while allowing upper-arm runtime pose application when pose payload prerequisites are present.

## Problem Context
- Repro context:
  - host: `WpfHost`
  - avatar: MIQ (`sourceExt=.vrm`)
  - symptom: slider values changed in UI but arm bones did not move visually.
- Existing runtime behavior before this fix:
  - `ApplyArmPoseToAvatar(...)` used `ShouldApplyStaticSkinningForAvatarMeshes(...)`.
  - In `auto` mode, VRM-derived MIQ returned `false` in static-skinning policy to prevent mesh-space mismatch risk.
  - Arm pose path exited early even when pose offsets were submitted successfully from host.

## Root Cause
1. Policy coupling:
   - arm-pose gate reused mesh static-skinning policy.
2. Safety intent mismatch:
   - mesh static-skinning policy intentionally conservative for `sourceExt=.vrm` in MIQ auto mode.
   - arm-pose application should not be blocked by that same conservative mesh decision.
3. Result:
   - host side input path (`SetPoseOffset -> nc_set_pose_offsets`) succeeded, but native apply path skipped.

## Implementation Changes
- File: `src/nativecore/native_core.cpp`

### 1) Added dedicated arm-pose policy resolver
- New function: `ShouldApplyArmPoseForAvatar(const AvatarPackage& avatar_pkg)`
- Env semantics retained:
  - `ANIMIQ_MIQ_ENABLE_STATIC_SKINNING=on|1|true|yes` => enable
  - `...=off|0|false|no` => disable
  - unset/auto => enable only when:
    - `avatar_pkg.source_type == AvatarSourceType::Miq`
    - `skin_payloads` not empty
    - `skeleton_payloads` not empty
    - `skeleton_rig_payloads` not empty
- Design intent:
  - keep arm pose decision independent from mesh static-skinning decision.

### 2) Switched arm-pose gate usage
- Updated `ApplyArmPoseToAvatar(...)`:
  - before: `ShouldApplyStaticSkinningForAvatarMeshes(avatar_pkg)`
  - after: `ShouldApplyArmPoseForAvatar(avatar_pkg)`
- Kept existing policy-disabled warning contract:
  - code: `ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY`

### 3) Added payload-missing diagnostic warning
- In `ApplyArmPoseToAvatar(...)`, when required pose payloads are missing:
  - message:
    - `W_RENDER: ARM_POSE_PAYLOAD_MISSING: arm pose skipped due to missing skin/skeleton/rig payload.`
  - code: `ARM_POSE_PAYLOAD_MISSING`
- Purpose:
  - separate policy-disabled vs payload-incomplete skip reasons in diagnostics.

## Compatibility and Risk
- No public API/ABI changes.
- Host interop contract unchanged (`nc_set_pose_offsets` path unchanged).
- Mesh static-skinning conservative policy for VRM-derived MIQ remains unchanged to minimize render regression risk.
- Behavior change is limited to arm-pose apply decision in native runtime.

## Verification
- Build:
  - `cmake --build NativeAnimiq/build --config Release --target nativecore` => PASS
  - `dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -c Release -v minimal` => PASS
    - first attempt hit sandboxed network restore error (`NU1301`)
    - rerun with network-enabled execution succeeded
- Source checks:
  - verified new gate function addition and usage in `ApplyArmPoseToAvatar(...)`
  - verified new warning code `ARM_POSE_PAYLOAD_MISSING` emission path

## Expected Runtime Behavior
- MIQ avatars with VRM-origin metadata in `auto` mode:
  - arm slider input should now produce visible upper-arm movement when pose payload prerequisites are valid.
- Forced policy off:
  - arm pose remains skipped with explicit policy warning.
- Incomplete payload:
  - arm pose skipped with explicit payload-missing warning for triage clarity.
