# NativeCore Fallback Hardening: Dominant-Cause Warning Convergence (2026-03-06)

## Summary

This update hardens NativeCore runtime behavior for mixed-quality VRM/MIQ avatars by prioritizing feature survivability and converging diagnostics to one dominant cause per family.

Observed operational issue before this change:

- runtime remained alive, but warning codes accumulated stale reasons across frames,
- arm/shadow root-cause visibility could drift from current state,
- VRM expression fallback lacked a warning-code signal while MIQ emitted one.

Implemented outcome:

- warning families (arm/shadow) now emit an exclusive dominant reason,
- arm-pose disable paths resolve to explicit format/payload/policy causes,
- warning classification includes feature-gate families and native submit failures,
- VRM expression fallback now emits a warning code for parity with MIQ.

## Problem Details

In repeated render/update cycles, `warning_codes` could preserve previous values while a new dominant reason became true (for example, shadow reason transition from material-lack to fallback mode). UI/diagnostic surfaces that prioritize `last_warning_code` could still show stale family context.

For arm pose, the disable path was primarily represented as policy-disabled when auto mode returned false, even when the true actionable reason was non-MIQ format or missing payload completeness.

For expression fallback, VRM emitted only warning text while MIQ emitted both text and warning code, reducing parity for downstream diagnostics parsing.

## Implementation

### 1) Exclusive warning management helpers

Updated:

- `src/nativecore/native_core.cpp`

Added:

- `RemoveAvatarWarningCode(...)`
- `PushAvatarWarningExclusive(...)`

Behavior:

- remove previous codes/messages in the same family before pushing the new dominant code,
- preserve unique warning semantics for non-exclusive families.

### 2) Warning metadata classification expansion

Updated:

- `src/nativecore/native_core.cpp` (`ClassifyWarningCode(...)`)

New render warning families:

- `ARM_POSE_*`
- `SHADOW_DISABLED_*`
- `TRACKING_*`
- `EXPRESSION_COUNT_ZERO`
- `SHADOW_PASS_NOT_REPORTED`

New critical render error family:

- `NC_SET_*`

Effect:

- severity/category fields stop defaulting to `unknown` for these runtime policy/recovery codes.

### 3) Arm-pose disable reason tightening

Updated:

- `src/nativecore/native_core.cpp` (`ApplyArmPoseToAvatar(...)`)

Disable-path dominance now maps to exactly one actionable code:

- `ARM_POSE_FORMAT_UNSUPPORTED` for non-MIQ avatars,
- `ARM_POSE_PAYLOAD_MISSING` for incomplete skin/skeleton/rig payloads,
- `ARM_POSE_DISABLED_BY_STATIC_SKINNING_POLICY` when payload-complete but policy-gated.

Effect:

- diagnostics and host gate interpretation can align with actual operator action.

### 4) Shadow disable dominant-cause convergence

Updated:

- `src/nativecore/native_core.cpp` (render queue diagnostics)

Shadow codes are now emitted with mutual exclusivity:

- `SHADOW_DISABLED_TOGGLE_OFF`
- `SHADOW_DISABLED_FAST_FALLBACK`
- `SHADOW_DISABLED_NO_SHADOW_PASS_MATERIAL`
- `SHADOW_DISABLED_SHADOW_DRAW_EMPTY`

Effect:

- last warning consistently reflects current dominant shadow gate.

### 5) VRM expression fallback warning-code parity

Updated:

- `src/nativecore/native_core.cpp` (`nc_load_avatar(...)`)

Added:

- `VRM_EXPRESSION_FALLBACK_APPLIED` when runtime injects fallback expressions for VRM.

Retained:

- `MIQ_EXPRESSION_FALLBACK_APPLIED` for MIQ.

Effect:

- fallback instrumentation is aligned across VRM and MIQ paths.

## Verification

Executed:

```powershell
cmake --build AnimiqCore/build_plan_impl --config Release --target nativecore
```

Observed:

- build succeeded and produced `nativecore.dll`,
- no compile/link errors after fallback/warning lifecycle changes.

## Compatibility / Risk Notes

- Public API signatures unchanged.
- Warning-code output is additive/refined; consumers that hardcode old code subsets should keep default handling.
- Exclusive family emission removes stale historical codes for arm/shadow from in-memory package warning state by design; this improves current-state diagnostics but changes historical accumulation behavior.
