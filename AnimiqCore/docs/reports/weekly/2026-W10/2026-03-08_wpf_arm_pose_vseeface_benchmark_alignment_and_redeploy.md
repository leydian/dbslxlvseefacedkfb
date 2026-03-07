# 2026-03-08 - WPF arm pose VSeeFace benchmark alignment and redeploy

## Summary

This pass stabilizes slider-driven arm raise/lower behavior in WPF by aligning runtime policy with practical VSeeFace behavior:

- drive upper-arm directly
- allow only relaxed helper follow for twist/sleeve bones
- disable lower-arm/hand/wrist auto-coupling on slider path

The pass also closes sleeve-follow regressions and republishes `dist/wpf` with the updated `nativecore.dll`.

## User-visible issues reproduced

1. Arm slider could spin arms instead of clean up/down motion.
2. Arm chain could bend unnaturally (elbow-like collapse) during simple raise/lower.
3. Clothing sleeves could remain in bind pose while body/arm moved.
4. Over-broad name fallback could rotate non-arm parts.

## Root causes

1. Pose application mixed global/local expectations on arm path, causing axis-intent mismatch on some rigs.
2. Arm fallback name matching was too broad in earlier iterations.
3. Some clothing/helper rigs lacked usable humanoid IDs, so sleeve helper bones were skipped.
4. Arm-pose collapse/tube rejection heuristics were too strict for certain helper meshes and caused false bind-pose rollback.

## Implementation details

## 1) Local-space arm pose application with hierarchy recomposition

File:
- `src/nativecore/native_core.cpp`

Changes:
- Arm pose path now constructs/updates local bone transforms, applies pose offsets in local space, then rebuilds globals in hierarchy order (`child * parent`) before static skinning.
- This replaces direct pose multiply on global matrices for arm edits.

Effect:
- More consistent behavior across rigs with different helper/parent layouts.

## 2) Arm axis remap to remove spin-like behavior

File:
- `src/nativecore/native_core.cpp`

Changes:
- Arm-chain bones map operator-facing pitch into arm-lift axis intent.
- Left/right side sign is mirrored appropriately.

Effect:
- Slider now behaves as arm raise/lower instead of visible roll/spin on target models.

## 3) Humanoid lookup fallback and strict token matching

File:
- `src/nativecore/native_core.cpp`

Changes:
- If humanoid ID lookup fails, fallback checks normalized bone names.
- Matching was hardened to boundary-aware tokens to reduce accidental non-arm hits.
- Generic broad token usage that caused false positives was removed.

Effect:
- Better helper-bone coverage for clothing rigs without reviving non-arm over-rotation.

## 4) Final coupling policy (VSeeFace-like)

File:
- `src/nativecore/native_core.cpp`

Final applied policy:
- UpperArm: `1.00`
- Twist/Sleeve helpers: `0.35`
- Shoulder/LowerArm/Hand/Wrist auto-coupling: `0.00` in slider path

Rationale:
- Mirrors practical anti-twist strategy visible in VSeeFace release history while preserving sleeve follow.

## 5) Arm reject guard tuning for sleeve follow

File:
- `src/nativecore/native_core.cpp`

Changes:
- Arm path keeps hard safety rejects for catastrophic outputs:
  - non-finite vertex output
  - extreme extent/abs/volume explosion
- Mild collapse/tube heuristics were removed from hard reject in arm pose path to prevent false sleeve rollback.

Effect:
- Sleeves are less likely to remain frozen in bind pose during normal arm movement.

## Benchmark/Reference notes

Reference used:
- `VSeeFace-v1.13.38c2/VSeeFace/Release notes.txt`

Observed relevant policy signals:
- upper-arm twist fix references
- twist relaxer fixes
- wrist reception separation from arm/shoulder
- arm-angle slider behavior fixes

Interpretation applied to runtime:
- keep arm-angle path upper-arm-centric
- keep helper follow relaxed
- avoid full-chain forced coupling for slider edits

## Verification

Native build:
- `cmake --build AnimiqCore/build --config Release --target nativecore`
- Result: PASS

WPF redeploy:
- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild -NoRestore`
- Result: PASS
- `WPF launch smoke`: PASS

Integrity check:
- `build/Release/nativecore.dll` and `dist/wpf/nativecore.dll` hashes match in:
  - `build/reports/host_publish_latest.txt`

## Output artifacts

- `dist/wpf/WpfHost.exe`
- `dist/wpf/nativecore.dll`
- `build/reports/host_publish_latest.txt`
- `build/reports/wpf_launch_smoke_latest.txt`

## Residual risk

- Models with uncommon sleeve helper naming may still need token extension.
- If a rig uses atypical arm local axes, side-specific axis remap may need per-avatar calibration in future.

