# XAV2 TryLoad Strict Option + Runtime Tests + Deterministic Gate Report (2026-03-03)

## Scope

This update completes the next hardening step after initial diagnostics rollout:

- add an option-based strict validation path for Unity runtime loading
- add Unity runtime tests for `Xav2RuntimeLoader`
- make fixed-valid XAV2 gate inputs deterministic using a VRM allowlist
- sync docs/contracts with the new behavior

## Implemented Changes

### 1) Option-based strict runtime validation

Files:

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`

Changes:

- Added `Xav2LoadOptions`:
  - `StrictValidation` (`false` by default)
- Added `Xav2LoadErrorCode.StrictValidationFailed`
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

- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/VsfClone.Xav2.Runtime.Tests.asmdef` (new)
- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs` (new)

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

## 3) Deterministic fixed-valid XAV2 gate generation

Files:

- `tools/vxavatar_sample_report.ps1`
- `tools/vxavatar_quality_gate.ps1`

Changes:

- Added allowlist-first input contract:
  - `-FixedXav2FromVrmAllowlist`
  - `-FixedXav2FromVrmCount`
- Default allowlist (5 VRMs) is embedded in both scripts.
- In `-UseFixedSet` or `-Profile full`:
  - missing allowlist entries now fail gate input preparation.
- Existing fallback behavior remains available when allowlist is empty or not used.

## Documentation Sync

Files:

- `README.md`
- `unity/Packages/com.vsfclone.xav2/README.md`
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

- `FixedXAV2=5`
- `CorruptXAV2=2`

## Notes

- XAV2 file format version remains `v1`.
- Existing non-strict behavior is preserved for backward compatibility.
