# MIQ TryLoad Strict Option + Runtime Tests + Deterministic Gate Report (2026-03-03)

## Scope

This update completes the next hardening step after initial diagnostics rollout:

- add an option-based strict validation path for Unity runtime loading
- add Unity runtime tests for `MiqRuntimeLoader`
- make fixed-valid MIQ gate inputs deterministic using a VRM allowlist
- sync docs/contracts with the new behavior

## Implemented Changes

### 1) Option-based strict runtime validation

Files:

- `unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`
- `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`

Changes:

- Added `MiqLoadOptions`:
  - `StrictValidation` (`false` by default)
- Added `MiqLoadErrorCode.StrictValidationFailed`
- Added overload:
  - `TryLoad(path, out payload, out diagnostics, options)`
- Preserved old signatures:
  - `Load(path)`
  - `TryLoad(path, out payload, out diagnostics)`
- Strict-mode fail-upgrade policy:
  - unknown sections fail
  - trailing bytes warnings fail
  - ref/payload mismatch diagnostics fail
  - non-strict mode keeps warning-based behavior for compatibility

### 2) Unity runtime test coverage

Files:

- `unity/Packages/com.animiq.miq/Tests/Runtime/Animiq.Miq.Runtime.Tests.asmdef` (new)
- `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs` (new)

Added test scenarios:

- valid payload success (`runtime-ready`)
- magic mismatch failure
- version mismatch failure
- manifest truncation failure
- section truncation failure
- legacy material decode compatibility (without `shaderVariant`)
- unknown section:
  - non-strict success + warning
  - strict failure
- strict failure on ref/payload mismatch

## 3) Deterministic fixed-valid MIQ gate generation

Files:

- `tools/vxavatar_sample_report.ps1`
- `tools/vxavatar_quality_gate.ps1`

Changes:

- Added allowlist-first input contract:
  - `-FixedMiqFromVrmAllowlist`
  - `-FixedMiqFromVrmCount`
- Default allowlist (5 VRMs) is embedded in both scripts.
- In `-UseFixedSet` or `-Profile full`:
  - missing allowlist entries now fail gate input preparation.
- Existing fallback behavior remains available when allowlist is empty or not used.

## Documentation Sync

Files:

- `README.md`
- `unity/Packages/com.animiq.miq/README.md`
- `CHANGELOG.md`

Changes:

- Documented strict option load path.
- Documented runtime test location and local execution intent.
- Documented deterministic allowlist gate policy.

## Verification

Executed:

- `powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`

Result:

- Gate A/B/C/D/E/F/G = `PASS`
- Overall = `PASS`

Coverage snapshot:

- `FixedMIQ=5`
- `CorruptMIQ=2`

## Notes

- MIQ file format version remains `v1`.
- Existing non-strict behavior is preserved for backward compatibility.
