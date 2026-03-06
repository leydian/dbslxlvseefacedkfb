# VRM SpringBone Runtime + MToon Advanced Binding Uplift (2026-03-06)

## Scope

This report covers the implementation pass that upgraded VRM runtime compatibility in two areas:

- SpringBone: metadata-only visibility to runtime-ready payload + native runtime diagnostics integration.
- MToon: core binding to expanded advanced typed parameter extraction and diagnostics visibility.

In scope:

- native API/interop diagnostics contract expansion
- VRM loader SpringBone payload extraction and MToon advanced typed parameter uplift
- native runtime secondary motion quality/robustness upgrades
- VRM gate expansion (GateH/GateI/GateJ)

Out of scope:

- full VRM SDK-equivalent skeletal constraint solver parity
- complete MToon shader parity for all keyword/feature combinations

## Implemented Changes

### 1) Native diagnostics/API contract expansion

- `include/animiq/nativecore/api.h`
  - extended `NcAvatarInfo` with:
    - `spring_active_chain_count`
    - `spring_corrected_chain_count`
    - `spring_disabled_chain_count`
    - `spring_unsupported_collider_chain_count`
    - `spring_avg_substeps`
    - `mtoon_advanced_param_material_count`
    - `mtoon_fallback_material_count`
  - extended `NcSpringBoneInfo` with:
    - `active_chain_count`
    - `corrected_chain_count`
    - `disabled_chain_count`
    - `unsupported_collider_chain_count`
    - `avg_substeps`
- `host/HostCore/NativeCoreInterop.cs`
  - synchronized C# interop structs to preserve native/managed layout parity.

### 2) SpringBone runtime path refinement in nativecore

- `src/nativecore/native_core.cpp`
  - secondary motion chain/state model extended with:
    - fixed-step accumulator
    - unsupported collider counters
    - per-frame substep diagnostics
  - improved mesh targeting:
    - rig-bone match path first (`skeleton_rig_payloads`),
    - name-token fallback second.
  - solver pass uplift:
    - fixed-step integration target (`120Hz`) with bounded substeps,
    - damping and offset-length constraints,
    - stability guard for frame spikes.
  - warning-code split for VRM path:
    - `VRM_SPRING_AUTO_CORRECTED`
    - `VRM_SPRING_CHAIN_DISABLED`
    - `VRM_SPRING_UNSUPPORTED_COLLIDER`
  - retained non-fatal fallback posture for malformed/partial payloads.

### 3) VRM loader SpringBone payload extraction

- `src/avatar/vrm_loader.cpp`
  - added runtime payload extraction path for:
    - VRM1 `extensions.VRMC_springBone`
      - colliders -> `physics_colliders`
      - springs/joints -> `springbone_payloads`
    - VRM0 `extensions.VRM.secondaryAnimation`
      - colliderGroups/boneGroups -> runtime payloads
  - preserved legacy summary extraction (`SpringBoneSummary`) and added payload-level warnings.

### 4) MToon advanced typed parameter uplift

- `src/avatar/vrm_loader.cpp`
  - extended material parse with advanced MToon-related fields:
    - matcap color/texture/strength
    - outline width/lighting mix
    - UV animation speeds and mask texture
  - promoted VRM material payloads to typed contract defaults:
    - `material_param_encoding = typed-v1`
    - `typed_schema_version = 1`
  - expanded typed payload emission:
    - additional float/color/texture parameters for matcap/outline/uv animation.
  - missing-feature reporting granularity refined to:
    - `MToon outline`
    - `MToon uv animation`
    - `MToon matcap`

### 5) Tooling and gates

- `tools/avatar_tool.cpp`
  - added output fields:
    - `SpringPayloads`
    - `PhysicsColliders`
    - `MtoonAdvancedMaterials`
    - `MtoonFallbackMaterials`
- `tools/vrm_quality_gate.ps1`
  - expanded gate matrix with:
    - GateH: spring payload completeness
    - GateI: spring runtime activation readiness
    - GateJ: spring payload stability guard
  - tuned checks to fixed5 realities:
    - require at least one spring-payload sample for GateH
    - require at least one spring+collider sample for GateI

## Verification Summary

Executed:

- `cmake --build NativeAnimiq/build --config Release --target nativecore avatar_tool` : PASS
- `powershell -ExecutionPolicy Bypass -File .\NativeAnimiq\tools\vrm_quality_gate.ps1 -SampleDir .\sample -AvatarToolPath .\NativeAnimiq\build\Release\avatar_tool.exe -Profile fixed5` : PASS
  - GateA..GateJ all PASS
  - Overall PASS
- `dotnet build NativeAnimiq/host/HostCore/HostCore.csproj -c Release` : PASS

Representative output spot checks:

- `sample/NewOnYou.vrm`
  - `SpringPayloads: 1`
  - `PhysicsColliders: 57`
  - `MtoonAdvancedMaterials: 12`
- `sample/Kikyo_FT Variant.vrm`
  - `SpringPayloads: 0`
  - `PhysicsColliders: 0`
  - `SpringBonePresent: true`

## Known Risks or Limitations

- Some VRM assets expose spring metadata but do not yield usable spring runtime payloads in this loader slice; gate policy now validates readiness at cohort level instead of requiring per-sample payload presence.
- Spring runtime pass remains a pragmatic deformation-based solver, not full SDK-equivalent joint/constraint parity.
- MToon advanced binding coverage improved substantially, but complete keyword-level parity is still not guaranteed.

## Next Steps

1. Add deterministic VRM fixtures for `metadata-present-but-no-runtime-payload` and `full spring payload` classes, then assert both policy paths in gate automation.
2. Expand collider runtime handling from sphere/capsule-first assumptions toward stricter shape-specific behavior.
3. Introduce MToon advanced parity checklist (outline/matcap/uv-anim per sample) with acceptance thresholds per profile.
