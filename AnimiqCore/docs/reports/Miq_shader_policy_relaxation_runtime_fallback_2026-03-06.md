# MIQ Shader Policy Relaxation + Runtime Fallback + CI Strict Lane (2026-03-06)

## Scope

- Apply the agreed plan to improve Unity-side success rate by relaxing default shader failure behavior.
- Keep CI parity enforcement strict to preserve release quality guardrails.
- Cover code changes, behavior deltas, and test expectations for this specific policy change.
- Out of scope: format version/schema changes (`.miq` wire shape), non-shader parity contracts, host/native runtime behavior outside Unity package.

## Implemented Changes

- Runtime load options expanded with shader policy selector.
  - Added `MiqShaderPolicy` with:
    - `WarnFallback` (default)
    - `Fail`
  - Added `MiqLoadOptions.ShaderPolicy` defaulting to `WarnFallback`.
  - Code path: `unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`

- Runtime loader unsupported-shader handling now policy-driven.
  - Previous behavior: unsupported shader family always failed with `ParityContractViolation`.
  - New behavior:
    - `WarnFallback`: continue load, force material `ShaderFamily` to `standard`, append warning code `MIQ_SHADER_FAMILY_FALLBACK`.
    - `Fail`: preserve prior hard-fail behavior with `ParityContractViolation` + `CriticalParityViolation = true`.
  - Code path: `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`

- Editor export default UX changed to relaxed-first.
  - `Tools/Animiq/MIQ/Export Selected AvatarRoot` now uses relaxed export options (`FailOnMissingShader=false`).
  - Strict path remains explicitly available as:
    - `Tools/Animiq/MIQ/Export Selected AvatarRoot (Strict)`
  - Code path: `unity/Packages/com.animiq.miq/Editor/MiqExportMenu.cs`

- CI parity probe explicitly pinned to strict shader policy.
  - `MiqCiQuality.RunParityProbe()` now calls runtime loader with `ShaderPolicy = Fail`.
  - This keeps parity gate behavior strict even after default runtime policy became relaxed.
  - Code path: `unity/Packages/com.animiq.miq/Editor/MiqCiQuality.cs`

- Runtime tests updated for new contract.
  - Replaced previous "unsupported shader always fails" expectation with two explicit cases:
    - default policy => success + fallback warning code
    - fail policy => parity violation fail
  - Code path: `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`

## Verification Summary

- Local verification performed:
  - Source diff review for all touched files in Unity package runtime/editor/test paths.
  - Policy wiring confirmation:
    - runtime default = `WarnFallback`
    - CI parity path = `Fail`
    - editor default export = relaxed

- Not executed in this session:
  - Unity EditMode test runner
  - `tools/unity_miq_validate.ps1`
  - `tools/miq_parity_gate.ps1`

- Reason:
  - No confirmed Unity project path/editor gate execution context in this run.

## Known Risks or Limitations

- Visual parity may degrade on unsupported shader families under default runtime policy because material family is canonicalized to `standard`.
- If downstream tooling assumed unsupported shaders always throw, behavior now differs unless `ShaderPolicy=Fail` is set explicitly.
- CI remains strict by design; local/manual load may now pass while strict parity gate still fails.

## Next Steps

1. Run Unity EditMode runtime tests to validate behavior in target editor environment.
2. Execute `tools/miq_parity_gate.ps1` and confirm strict lane still blocks unsupported shader samples.
3. Execute `tools/unity_miq_validate.ps1` on the target Unity project and archive artifacts in `build/reports`.
