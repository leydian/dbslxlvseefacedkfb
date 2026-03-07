# VSeeFace Parity: Global Defaults and VRM-Origin ArmPose Guard (2026-03-07)

## Scope

- Stabilize shared load/render rules for VRM/MIQ so initial visual output is closer to VSeeFace baseline.
- Apply global default adjustments for framing/background/quality.
- Keep similarity-first arm pose behavior for MIQ while adding VRM-origin rollback safety.
- Out of scope: WPF view-layer redesign, Unity exporter changes, and unrelated workspace modifications.

## Implemented Changes

### 1) Global render defaults aligned to parity-oriented baseline

Updated defaults in both Host and Native so first-load behavior is consistent across initialization paths:

- `host/HostCore/NativeCoreInterop.cs`
  - `BuildBroadcastPreset()`
  - `FramingTarget: 0.72 -> 0.80`
  - `Headroom: 0.12 -> 0.10`
  - `FovDeg: 45 -> 40`
  - `Background: DarkBlue RGB -> NeutralGray RGB(0.55, 0.55, 0.55)`
  - `QualityProfile: Default -> Balanced`
- `src/nativecore/native_core.cpp`
  - `MakeDefaultRenderQualityOptions()`
  - Same numeric changes as above for runtime-side default parity.
- `host/HostCore/HostController.cs`
  - Initial `RenderState` and post-shutdown reset background preset set to `NeutralGray` to avoid host/native default divergence.
- `host/HostCore/RenderPresetStore.cs`
  - Built-in `"Broadcast Default"` preset updated to the same baseline (`0.80/0.10/40`, neutral gray).

Result: load/start/shutdown/default preset paths now converge on one visual baseline instead of mixed defaults.

### 2) Neutral expression bootstrap when tracking payload is absent

Added host-side neutral expression application to avoid exaggerated non-neutral face state at load/idle.

- `host/HostCore/HostController.cs`
  - Added `TryApplyNeutralExpressionWeightsForActiveAvatar()`.
  - Implementation:
    - query expression catalog via `nc_get_expression_count` + `nc_get_expression_infos`
    - submit zeroed weights via `nc_set_expression_weights`
  - Called after successful avatar load (`LoadAvatar`) and in tick when tracking expression weights are unavailable.
  - Added operation tracking keys:
    - `SetExpressionWeightsNeutralLoadAvatar`
    - `SetExpressionWeightsNeutral`
  - Default quality-profile fallback in `ApplySelectedQualityProfile()` changed from `Default` to `Balanced`.

Result: when tracker input is missing, expression state is actively reset to neutral instead of drifting by prior/default runtime state.

### 3) Similarity-first arm pose enablement + VRM-origin rollback guard

Arm-pose policy remains payload-based, with an additional safety rollback path for VRM-origin MIQ.

- `src/nativecore/native_core.cpp`
  - `ShouldApplyArmPoseForAvatar(...)`
    - removed VRM-origin hard block in auto mode.
    - policy now remains payload-completeness driven in auto mode.
  - Added rollback state:
    - `CoreState::arm_pose_auto_rollback_handles`
  - Added helper:
    - `ResetAvatarMeshesToBindPose(...)`
  - `ApplyArmPoseToAvatar(...)` enhancements:
    - if handle previously rollbacked: skip arm pose and emit exclusive warning code `ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN`.
    - monitor VRM-origin risk signals:
      - low hair/head alignment score
      - `MIQ_SKINNING_EXTENT_GUARD` trigger during pose application
    - on guard trigger:
      - insert handle into rollback set
      - reset mesh buffers to bind pose
      - emit exclusive rollback warning with reason token.
  - lifecycle cleanup:
    - rollback handle set cleared/erased on init/shutdown/load/unload/create/destroy render resources.

Result: VRM-origin MIQ can use arm pose for similarity, but auto-rolls back to safe state when instability signals appear.

### 4) Host feature-gate reason mapping for new warning code

- `host/HostCore/HostFeatureGates.cs`
  - Added reason text + operator action hint mapping for:
    - `ARM_POSE_AUTO_ROLLBACK_VRM_ORIGIN`

Result: runtime diagnostics show explicit cause/action instead of generic policy failure text.

## Verification Summary

- Native build:
  - `cmake --build AnimiqCore/build_hotfix --config Release --target nativecore`
  - Outcome: `PASS` (`nativecore.dll` built).
- HostCore build:
  - restore/build blocked by package source/network constraints in this environment.
  - Observed:
    - missing local mirror path (`NU1301`) in initial attempt
    - nuget index/package fetch failures (`NU1801`, `NU1101`) in override attempts.
  - Outcome: `BLOCKED (environmental restore/network source issue)`.

## Known Risks or Limitations

- Neutral-expression fallback currently submits zero weights for entire catalog when tracking payload is absent; this is intentional for deterministic idle-neutral behavior but may suppress authored non-tracking idle expression style.
- Host project compile verification could not be completed in this environment due to restore source issues; only native compile was validated.
- Existing user-custom preset files are not migrated; only built-in defaults and runtime defaults changed.

## Next Steps

1. Re-run `HostCore` build in an environment with valid NuGet/mirror connectivity and confirm no compile/runtime regressions.
2. Run VRM/MIQ parity captures for target avatar set and compare initial pose/expression/framing against VSeeFace screenshots.
3. If rollback frequency is high on specific avatars, tune guard thresholds (`hair_head_alignment_score`) with corpus-based telemetry.
