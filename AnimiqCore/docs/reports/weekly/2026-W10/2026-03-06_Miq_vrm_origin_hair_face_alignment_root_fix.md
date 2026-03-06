# MIQ VRM-Origin Hair/Face Alignment Root Fix (2026-03-06)

## Summary
- Addressed a user-facing regression where VRM-origin `.miq` avatars rendered with hair/face positional desync.
- Confirmed this was not a stale runtime DLL issue and not resolved by simple re-export alone.
- Final stable state was achieved by locking VRM-origin MIQ out of runtime re-skinning paths.

## Symptoms and Context
- Observed in WPF preview/output after loading `개인작10-2.miq`.
- User-reported behavior across retries:
  - avatar sink issue improved in prior pass
  - hair/face mismatch persisted
  - intermediate regressions included missing shoes during aggressive mesh heuristics
- Runtime snapshot remained healthy at parser/load level:
  - `Format: MIQ`
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - no critical warnings

## Investigation Timeline
1. Runtime module path/staleness verified:
   - `dist/wpf/nativecore.dll` matched `build/Release/nativecore.dll` path/timestamp/hash.
2. Re-export path validated:
   - ran `vrm_to_miq` on `sample\\개인작10-2.vrm`
   - replaced root `개인작10-2.miq` with re-export output
   - result remained unchanged visually.
3. Source differential check:
   - VRM direct load reported `VRM_NODE_TRANSFORM_APPLIED: meshes=6`
   - MIQ load did not carry equivalent runtime warning context, despite full payload counts.
4. Runtime policy isolation:
   - tested combinations of static skinning on/off and arm-pose path on/off
   - stable success only when VRM-origin MIQ re-skinning paths were both disabled.

## Implemented Fix
- File: `src/nativecore/native_core.cpp`
- Policy locks:
  - `ShouldApplyStaticSkinningForAvatarMeshes(...)`
    - force `false` for `source_type == Miq && source_ext == ".vrm"`
  - `ShouldApplyArmPoseForAvatar(...)`
    - force `false` for `source_type == Miq && source_ext == ".vrm"` in auto mode

## Why This Is the Root Fix
- The failure pattern was caused by runtime re-skinning divergence (mesh-space inconsistency), not by file I/O/deploy mismatch.
- Re-export determinism showed no effective data change from same source.
- Locking re-skinning paths restored coherent face/hair/body placement without reintroducing sink/shoe regressions in the final user validation.

## Verification Evidence
- Build:
  - `MSBuild build/nativecore.vcxproj /p:Configuration=Release /p:Platform=x64 /m` -> PASS
- Deploy:
  - copied `build/Release/nativecore.dll` to `dist/wpf/nativecore.dll`
  - SHA256 parity confirmed
- Tooling checks:
  - `avatar_tool.exe "<vrm>" --dump-warnings-limit=200` -> PASS
  - `avatar_tool.exe "<miq>" --dump-warnings-limit=200` -> PASS
- User validation:
  - final confirmation: "이제 잘돼!"

## Follow-up
- Keep current lock as safe default for VRM-origin MIQ until transform metadata can be persisted/replayed explicitly through exporter + loader contract.
- Preferred long-term direction:
  - encode transformed-node/mesh-space correction metadata in MIQ
  - apply deterministically at load time instead of runtime heuristic re-skinning.
