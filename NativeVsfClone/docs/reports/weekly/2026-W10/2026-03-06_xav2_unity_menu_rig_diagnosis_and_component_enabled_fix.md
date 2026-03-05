# XAV2 Unity Rig Diagnosis Menu and Component Enabled Compile Fix (2026-03-06)

## Scope
- Add an editor-facing rig diagnosis workflow for `.xav2` imports without changing default import behavior.
- Fix Unity compile break (`CS1061`) caused by `Component.enabled` access in physics payload extraction.
- Document the new diagnosis workflow and the baseline findings from the problematic sample.

## Implemented Changes
- Editor menu expansion:
  - Added `Tools/VsfClone/XAV2/Diagnose Rig (Strict/Fallback)...`.
  - Runs two imports on the same source file:
    - Strict (`FailOnRigDataMissing=true`, `RigRecoveryPolicy=Strict`)
    - Fallback (`FailOnRigDataMissing=false`, `RigRecoveryPolicy=Fallback`)
  - Shows side-by-side summary in dialog/log:
    - `success`, `partial`, `rigQuality`
    - `warnings`, `recoverable`, `rigDiagnostics`
    - extracted `rigCodes` (`XAV4_RIG_*`) from warning codes/messages/diagnostics.
  - Added lightweight decision guidance text:
    - strict fail + fallback success => source rig data regeneration path.
    - strict success + diagnostics > 0 => rig warning remediation required.
    - strict success + diagnostics 0 => rig path likely healthy.
- Compile fix in extractor:
  - Replaced direct `component.enabled` usage in SpringBone/PhysBone payload extraction.
  - Added safe helper:
    - `GetComponentEnabled(Component component)`
    - returns `behaviour.enabled` when `component is Behaviour`, else `true`.
  - Removes `CS1061` while preserving behavior for `Behaviour`-derived components.
- README updates:
  - Added new diagnosis menu entry.
  - Added short rig diagnosis workflow and interpretation guidance.

## Verification Summary
- Native baseline check on target sample:
  - Command:
    - `NativeVsfClone/build/Release/avatar_tool.exe 개인작11-3.xav2 --dump-warnings-limit=200`
  - Artifact:
    - `build/reports/xav2_rig_diag_baseline_2026-03-06.txt`
  - Result highlights:
    - Load succeeded (`Compat=full`)
    - No `XAV4_RIG_*` warning code from native loader path
    - Observed warning code: `XAV2_MATERIAL_SHADER_FAMILY_NOT_ALLOWED`
    - Additional warning: `XAV2_BLENDSHAPE_PARTIAL`
- Static code validation:
  - Confirmed all direct `component.enabled` references in `Xav2AvatarExtractors.cs` were replaced by helper usage.

## Known Risks or Limitations
- Unity Editor runtime verification (menu visibility and import dialog flow) must be confirmed in local Unity project session after script recompilation.
- Strict/Fallback diagnosis output quality depends on source file warning quality and rig diagnostic population.
- Existing unrelated working-tree changes were intentionally not altered.

## Next Steps
1. In Unity, run `Diagnose Rig (Strict/Fallback)...` on the failing `.xav2` and capture summary output.
2. If strict fails and fallback succeeds, regenerate source avatar/export with rig validation pass.
3. If strict succeeds with zero rig diagnostics, prioritize material/shader-family remediation (`XAV2_MATERIAL_SHADER_FAMILY_NOT_ALLOWED`).
