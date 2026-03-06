# 2026-03-06 - WPF arm chain coupling: shoulder/lower-arm/hand

## Summary
- Goal: remove unnatural arm raise/lower deformation by coupling upper-arm pitch with shoulder, lower-arm, and hand.
- UX policy: keep existing arm sliders unchanged (`Both`, `Left`, `Right`) and apply coupling internally.
- Result: upper-arm pose edits now propagate through the arm chain with stronger linkage while preserving existing preset/session contracts.

## Implementation
- Host pose model/API expansion:
  - Added pose bones in host/native interop:
    - `Left/RightShoulder`
    - `Left/RightLowerArm`
    - `Left/RightHand`
  - Files:
    - `host/HostCore/HostUiState.cs`
    - `host/HostCore/NativeCoreInterop.cs`
    - `include/animiq/nativecore/api.h`
    - `include/animiq/avatar/avatar_package.h`
- Host arm coupling logic:
  - File: `host/HostCore/HostController.cs`
  - Trigger: when `LeftUpperArm` or `RightUpperArm` pitch is set.
  - Coupling profile (strong):
    - shoulder = `0.55 * upper-arm pitch`
    - lower-arm = `0.85 * upper-arm pitch`
    - hand = `0.45 * upper-arm pitch`
  - Behavior:
    - pitch only is overwritten for linked bones.
    - yaw/roll for linked bones is preserved.
  - Pitch clamp profile:
    - upper/lower arm: `[-90, +90]`
    - shoulder/hand: `[-60, +60]`
    - other bones: `[-45, +45]`
- Preset compatibility:
  - File: `host/HostCore/PosePresetStore.cs`
  - Added new bones to default and normalized offset sets.
  - Backward compatibility: missing new-bone entries in old preset files are zero-filled.
- WPF pose UI:
  - Files:
    - `host/WpfHost/MainWindow.xaml`
    - `host/WpfHost/MainWindow.xaml.cs`
  - Added new bones to Pose combo list.
  - Added dynamic pitch slider min/max sync by selected pose bone category.
- Native pose application:
  - Files:
    - `src/avatar/vrm_loader.cpp`
    - `src/nativecore/native_core.cpp`
  - Extended VRM humanoid name mapping for new chain bones.
  - Static skinning pose apply path now includes shoulder/lower-arm/hand.
  - Pose-delta cache expanded to track all eight arm-chain nodes (L/R upper, shoulder, lower, hand).

## Verification
- `dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -v minimal`: PASS
- `cmake --build NativeAnimiq/build --target nativecore --config Debug`: PASS
- Notes:
  - NuGet restore required network-enabled execution in this environment.
  - Existing unrelated local workspace modifications were left untouched.

## Risk and Follow-up
- Coupling currently targets pitch-axis only; yaw/roll coupling is intentionally out of scope for this pass.
- WinUI pose bone picker parity can be added in a follow-up if WinUI direct pose editing for the new bones is required.
