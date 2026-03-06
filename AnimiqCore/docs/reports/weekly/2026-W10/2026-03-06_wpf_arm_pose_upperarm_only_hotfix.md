# 2026-03-06 - WPF arm pose upper-arm-only hotfix (deformation safety rollback)

## Summary

Applied a focused hotfix to remove arm-chain over-coupling that produced severe sleeve/arm deformation on some avatars.

- pose interaction policy is now upper-arm dominant:
  - left/right upper-arm pitch remains editable and filtered
  - shoulder/lower-arm/hand auto-coupling is disabled
- preset safety policy is now deterministic:
  - shoulder/lower-arm/hand pitch values are normalized to `0` during preset normalization
- native static skinning pose application now applies runtime arm pose only to upper-arm nodes

This change prioritizes deformation safety and predictable operator control over aggressive chain-driven motion.

## Problem Statement

- Recent chain-coupling behavior propagated upper-arm pitch to shoulder/lower-arm/hand with strong ratios.
- For specific rig/weight layouts, this produced visible mesh stretching and implausible arm silhouettes under normal slider operations.
- Operators requested behavior aligned with a simpler target reference where arm lift/lower feels natural without chain over-rotation artifacts.

## Implemented Changes

### 1) Host arm pose write path safety rollback

Updated:

- `host/HostCore/HostController.cs`

Details:

- Removed upper-arm-triggered linked pitch application:
  - deleted `ApplyArmChainCoupling(...)`
  - deleted `ApplyLinkedBonePitch(...)`
  - removed coupling constants (`shoulder/lower-arm/hand ratios`)
- `SetPoseOffset(...)` for upper arms now updates only the selected upper-arm pose offset plus existing filter pipeline.
- Existing arm input filtering path (deadband/smoothing/rate limit) remains active for upper-arm inputs.

### 2) Preset normalization hardening

Updated:

- `host/HostCore/PosePresetStore.cs`

Details:

- Added linked-arm-bone classification in normalization (`Left/RightShoulder`, `Left/RightLowerArm`, `Left/RightHand`).
- During preset normalization, linked arm bones are forced to neutral pitch:
  - `pitch = 0.0f`
  - yaw/roll still clamped and preserved
- Effect:
  - legacy presets carrying chain pitch values are automatically sanitized on load/save normalization paths
  - future preset application avoids reintroducing chain-induced deformation

### 3) Native static skinning arm-pose scope reduction

Updated:

- `src/nativecore/native_core.cpp`

Details:

- In `ApplyArmPoseToAvatar(...)`:
  - linked arm poses (`shoulder/lower-arm/hand`) are treated as neutral
  - humanoid pose application calls now target only:
    - `LeftUpperArm`
    - `RightUpperArm`
- Arm pose cache tracking remains in place to preserve no-op early-out optimization behavior.
- Existing mesh extent guardrails remain intact.

## Verification

### Build checks

- `dotnet build NativeAnimiq/host/HostCore/HostCore.csproj -v minimal`: PASS
  - first attempt failed under sandboxed NuGet network restriction (`NU1301`)
  - re-run with network-enabled elevated execution succeeded
- `cmake --build NativeAnimiq/build --config Release --target nativecore`: PASS

### Source checks

- Confirmed no references to removed arm chain coupling functions remain in `HostController.cs`.
- Confirmed native arm pose humanoid application now includes only left/right upper-arm calls.
- Confirmed preset normalization path deterministically neutralizes linked arm pitch channels.

## Compatibility and Risk

- Public APIs/interop signatures remain unchanged.
- Behavior change is runtime policy-only:
  - chain-driven articulation is intentionally disabled to prevent deformation regressions.
- Potential tradeoff:
  - some avatars may look less expressive in elbow/hand follow-through versus previous strong coupling.
  - this is accepted for stability-first operation and can be revisited behind an explicit opt-in mode later.
