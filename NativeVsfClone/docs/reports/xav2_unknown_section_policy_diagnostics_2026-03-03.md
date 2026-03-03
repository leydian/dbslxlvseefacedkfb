# XAV2 Unknown Section Policy + Structured Diagnostics Update (2026-03-03)

## Summary

This update implements the first production-hardening slice for the Unity XAV2 runtime loader:

- unknown section handling is now policy-driven (`Warn` / `Ignore` / `Fail`)
- warning output is now machine-readable via normalized warning code extraction
- runtime tests were expanded to lock behavior for each unknown-section policy mode

The change is additive and backward-compatible for existing call sites because default policy remains `Warn`.

## Scope

- Unity package: `unity/Packages/com.vsfclone.xav2`
- Runtime data model + loader behavior + runtime tests + package README

## Detailed Changes

### 1) Runtime options contract extension

File:
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`

Additions:
- `Xav2LoadErrorCode.UnknownSectionNotAllowed`
- `Xav2UnknownSectionPolicy` enum:
  - `Warn`
  - `Ignore`
  - `Fail`
- `Xav2LoadOptions.UnknownSectionPolicy` (default: `Warn`)
- `Xav2LoadDiagnostics.WarningCodes` (`List<string>`)

Impact:
- callers can explicitly choose forward-compatibility vs strict schema enforcement behavior for unknown section types
- diagnostics consumers can rely on stable code tokens instead of parsing full warning text

### 2) Loader behavior change for unknown sections

File:
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`

Additions:
- `HandleUnknownSection(...)` dispatches behavior by policy

Behavior by policy:
- `Warn`:
  - append warning `XAV2_UNKNOWN_SECTION: 0x....`
  - continue loading
- `Ignore`:
  - skip warning emission
  - continue loading
- `Fail`:
  - terminate load with `Xav2LoadErrorCode.UnknownSectionNotAllowed`

Compatibility:
- existing behavior preserved under default (`Warn`)

### 3) Structured warning code extraction

File:
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`

Additions:
- `AddWarningCode(...)`
- `AddWarningOrFail(...)` now extracts warning prefix before `:` and records into `diagnostics.WarningCodes`

Example:
- warning text: `XAV2_ASSET_MISSING: meshRef='mesh_0'`
- recorded code: `XAV2_ASSET_MISSING`

Intended use:
- dashboards, QA gates, and host-side diagnostics can aggregate by code without string-fragile matching

### 4) Runtime test coverage expansion

File:
- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

Updated/added tests:
- `TryLoad_UnknownSection_NonStrict_AllowsWithWarning`
  - now also verifies `WarningCodes` contains `XAV2_UNKNOWN_SECTION`
- `TryLoad_UnknownSection_IgnorePolicy_AllowsWithoutWarning`
  - verifies success + no unknown-section warning/code
- `TryLoad_UnknownSection_FailPolicy_Fails`
  - verifies fail path + `UnknownSectionNotAllowed`

Result:
- unknown section policy contract is explicitly locked by tests

### 5) Package documentation sync

File:
- `unity/Packages/com.vsfclone.xav2/README.md`

Updated:
- loader options section to include `UnknownSectionPolicy`
- diagnostics field list to include `WarningCodes`
- behavior table for `Warn` / `Ignore` / `Fail`

## Verification Status

Completed:
- static code-level verification and diff consistency check
- test additions compile-shape verified by symbol/path checks

Not executed in this environment:
- Unity EditMode runtime test execution

Recommended verification command (Unity Editor):
- open Test Runner and run `VsfClone.Xav2.Runtime.Tests`

## Risks and Follow-up

Low risk:
- default behavior is unchanged (`Warn`)
- new options are additive

Follow-up recommended:
- mirror `UnknownSectionPolicy` and structured warning code output in native C++ `xav2_loader` diagnostics path for Unity/native parity
