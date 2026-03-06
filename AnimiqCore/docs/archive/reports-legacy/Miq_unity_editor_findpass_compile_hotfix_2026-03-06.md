# MIQ Unity Editor Compile Hotfix (`FindPass` API mismatch) - 2026-03-06

## Summary

Resolved a Unity SDK editor compilation failure that caused the MIQ tools menu to disappear.

- user-facing symptom: `Tools > Animiq > MIQ` menu not shown
- compile error:
  - `MiqAvatarExtractors.cs(934,24): error CS1061: 'Shader' does not contain a definition for 'FindPass'`
- primary impact:
  - `Animiq.Miq.Editor` assembly compile stops
  - `[MenuItem(...)]` methods are not registered in Editor

## Root Cause

`BuildPassFlags(Material material)` in `MiqAvatarExtractors.cs` used `Shader.FindPass(...)`.

- this API call is not available on `UnityEngine.Shader` in the target Unity line (`2021.3.18f1+`)
- compiler fails before editor scripts can load
- MIQ menu paths under `Tools/Animiq/MIQ` become unavailable despite package being installed

## Implementation

Target file:
- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`

Changes applied:
1. Added explicit early return for `material == null`.
2. Replaced all `shader.FindPass(...) >= 0` checks with Unity-compatible checks:
   - `material.GetShaderPassEnabled("DepthOnly")`
   - `material.GetShaderPassEnabled("DepthForwardOnly")`
   - `material.GetShaderPassEnabled("ShadowCaster")`
   - `material.GetShaderPassEnabled("ForwardAdd")`
3. Preserved output string contract for pass flags:
   - base: `base`
   - optional suffixes: `|depth`, `|shadowcaster`, `|forwardadd`

## Compatibility/Behavior Notes

- no package public API changes (`com.animiq.miq` surface unchanged)
- no runtime loader/parser changes
- change scope is editor-only extraction diagnostics metadata (`passFlags` generation path)
- behavior shift:
  - pass detection now reflects enabled pass states through `Material.GetShaderPassEnabled(...)`

## Verification

Executed checks in workspace:

- `rg -n "FindPass\\(" unity/Packages/com.animiq.miq -S`
  - result: no remaining matches
- `rg -n "BuildPassFlags|GetShaderPassEnabled\\(" unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs -S`
  - result: updated calls found in pass flag builder

Expected Unity-side validation:
1. script recompile with no `CS1061` on `MiqAvatarExtractors.cs`
2. menu restored:
   - `Tools/Animiq/MIQ/Export Selected AvatarRoot`
   - `Tools/Animiq/MIQ/Export Selected AvatarRoot (Strict)`
   - `Tools/Animiq/MIQ/Import MIQ...`
   - `Tools/Animiq/MIQ/Diagnose Rig (Strict/Fallback)...`

## Risk

Low.

- fallback safety: method always returns at least `base`
- null guard reduces chance of secondary editor exceptions during extraction
- any future pass-flag precision tuning can be isolated to this helper without format contract changes
