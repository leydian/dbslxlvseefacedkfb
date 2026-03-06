# XAV2 Public SDK Packaging v1.0.0 Report (2026-03-06)

## Scope

This report summarizes the external-distribution hardening slice for
`com.vsfclone.xav2` with a focus on package readiness, public documentation,
sample onboarding, and release gating.

In scope:

- package metadata upgrade for public UPM consumption
- legal/notice and third-party attribution baseline
- public contract docs (`compatibility`, `migration`, `error-codes`)
- UPM sample onboarding paths
- release gate script for package integrity checks

Out of scope:

- expanding Unity baseline beyond `2021.3.18f1`
- URP/HDRP support
- native runtime rendering policy changes outside package contract docs

## Implemented Changes

### 1) Package metadata hardening and version baseline

Updated:

- `unity/Packages/com.vsfclone.xav2/package.json`

Changes:

- package version promoted from `0.1.0` to `1.0.0`
- added public metadata fields:
  - `documentationUrl`
  - `changelogUrl`
  - `licensesUrl`
  - `repository` (`type`, `url`)
- added UPM sample registrations:
  - `Runtime Load Sample`
  - `Export/Import Roundtrip Sample`

### 2) Public-facing README rewrite

Updated:

- `unity/Packages/com.vsfclone.xav2/README.md`

Changes:

- replaced internal-style scope notes with a 5-minute onboarding flow
- added runtime `TryLoad` diagnostics-first usage snippet
- documented explicit support matrix (`Unity 2021.3.18f1`, Built-in RP)
- documented load option defaults and warning/error handling flow
- linked public docs (`compatibility`, `migration`, `error-codes`)

### 3) Legal and attribution baseline

Added:

- `unity/Packages/com.vsfclone.xav2/LICENSE`
- `unity/Packages/com.vsfclone.xav2/NOTICE`
- `unity/Packages/com.vsfclone.xav2/ThirdPartyNotices.md`

Intent:

- establish minimum legal/notice footprint for external SDK distribution
- clarify shader-family compatibility references versus bundled third-party code

### 4) Public contract documentation

Added:

- `docs/public/compatibility.md`
- `docs/public/migration.md`
- `docs/public/error-codes.md`

Coverage:

- compatibility matrix and explicit non-goals for `1.0.0`
- `v0.x -> v1.0.0` migration expectations and checks
- runtime `ErrorCode`/`ParserStage`/`WarningCodes` operational handling guide

### 5) Sample onboarding assets

Added:

- `unity/Packages/com.vsfclone.xav2/Samples~/RuntimeLoadSample/RuntimeLoadSample.cs`
- `unity/Packages/com.vsfclone.xav2/Samples~/ExportImportRoundtripSample/Editor/Xav2RoundtripSampleMenu.cs`

Sample behavior:

- runtime sample:
  - loads `.xav2` via `TryLoad`
  - exposes strict/unknown-section policy fields
  - logs structured diagnostics on failure
- editor sample:
  - exports selected avatar root to temporary `.xav2`
  - imports back into project output root
  - surfaces success/failure through dialog and ping

### 6) Runtime API contract polish

Updated:

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`

Changes:

- `Xav2LoadOptions` and `Xav2LoadDiagnostics` surfaced as property-based contracts
- default contract values are explicit at declaration sites
- XML docs added for public loader entry points (`Load`, `TryLoad` overloads)

### 7) Release gate for package integrity

Added:

- `tools/xav2_package_release_gate.ps1`

Checks:

- required package artifacts exist (`README`, `LICENSE`, `NOTICE`, notices file)
- required public docs exist
- `package.json` contains release metadata links and repository
- sample registrations and sample directories are present

## Verification

Executed:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\xav2_package_release_gate.ps1
```

Observed:

- `PASS package=com.vsfclone.xav2 version=1.0.0`

## Risks and Follow-ups

- Unity EditMode/smoke test execution is still required as part of final public release candidate validation.
- License text is currently proprietary baseline wording; legal review may refine redistribution clauses.
- `1.0.0` support scope remains intentionally narrow (single Unity baseline + Built-in RP).

## Deliverable Snapshot

- package: public metadata + samples registered
- docs: onboarding + compatibility + migration + error contract published
- legal: minimum distribution artifacts added
- tooling: package release gate added and passing
