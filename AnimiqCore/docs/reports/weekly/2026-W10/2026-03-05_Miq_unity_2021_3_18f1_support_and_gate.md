# MIQ Unity SDK 2021.3.18f1 Support + Compatibility Gate Automation (2026-03-05)

## Scope

This update consolidates the Unity SDK support expansion work and the follow-up compatibility gate automation:

1. expand minimum supported Unity editor version to `2021.3.18f1`
2. harden package compile compatibility for older editor toolchains
3. add executable validation flow (EditMode tests + export/load smoke)
4. wire CI gate on self-hosted runner for support enforcement

## Implemented Changes

### 1) Package support contract expanded to `2021.3.18f1+`

Files:

- `unity/Packages/com.animiq.miq/package.json`
- `unity/Packages/com.animiq.miq/README.md`
- `README.md`

Changes:

- lowered package minimum Unity from `2022.3` to `2021.3`
- added `unityRelease: "18f1"` to pin support floor explicitly
- updated package/root documentation to describe support as `2021.3.18f1+`

### 2) C# compatibility hardening for Unity 2021.3 compile stability

Files:

- `unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqAvatarExtractors.cs`
- `unity/Packages/com.animiq.miq/Editor/MiqExportOptions.cs`

Changes:

- removed target-typed `new()` usages in package code
- replaced with explicit constructor forms (`new List<string>()`, etc.)
- runtime/export behavior unchanged; compile compatibility improved

### 3) Unity validation script for local/CI reproducible support checks

File:

- `tools/unity_miq_validate.ps1`

Behavior:

- validates required inputs:
  - Unity editor path (`UNITY_2021_3_18F1_EDITOR_PATH` or `-UnityEditorPath`)
  - Unity project path (`UNITY_MIQ_PROJECT_PATH` or `-UnityProjectPath`)
- step 1: runs Unity EditMode tests in batch mode
- step 2: runs smoke execute-method (`Animiq.Miq.Editor.MiqCiSmoke.Run`)
- writes deterministic artifacts:
  - `build/reports/unity_miq_editmode.log`
  - `build/reports/unity_miq_editmode_results.xml`
  - `build/reports/unity_miq_smoke.log`
  - `build/reports/unity_miq_smoke_report.json`
  - `build/reports/unity_miq_validation_summary.json`
  - `build/reports/unity_miq_validation_summary.txt`
- exits non-zero if any required check fails

### 4) Export/load smoke entrypoint for batch-mode CI

File:

- `unity/Packages/com.animiq.miq/Editor/MiqCiSmoke.cs`

Behavior:

- builds a minimal runtime AvatarRoot scene object in-memory
- exports `.miq` via `MiqExporter.Export(...)`
- verifies runtime load via `MiqRuntimeLoader.TryLoad(...)`
- enforces expected parser-ready state:
  - `ErrorCode=None`
  - `ParserStage=runtime-ready`
- emits structured smoke report JSON
- returns process code via `EditorApplication.Exit(0|1)`

### 5) CI gate workflow (self-hosted, Windows)

File:

- `.github/workflows/unity-miq-compat.yml`

Behavior:

- triggers on SDK/workflow/script path changes and manual dispatch
- runs on `self-hosted` Windows runner
- executes `tools/unity_miq_validate.ps1` with `2021.3.18f1` expectation
- always uploads generated validation artifacts
- intended as required PR gate for official support guarantee

### 6) Documentation and changelog sync

Files:

- `README.md`
- `unity/Packages/com.animiq.miq/README.md`
- `CHANGELOG.md`

Changes:

- documented support contract as CI-enforced validation
- added local reproduction command for maintainers
- recorded chronological implementation entries in changelog format

## Verification Summary

Executed in this environment:

- PowerShell parser check for `tools/unity_miq_validate.ps1`: PASS
- static review of workflow/script/execute-method path contracts: PASS
- file-level diff consistency check across SDK + docs + CI files: PASS

Not executed in this environment:

- Unity Editor runtime execution (`runTests` + `executeMethod`), because this shell environment does not include:
  - Unity editor binary path
  - external Unity project path used for package validation

## Known Limitations

- repository stores the SDK as a package; no full Unity project is versioned here for direct in-repo editor execution
- CI gate depends on self-hosted runner configuration quality:
  - editor path variable must be valid
  - project path variable must point to a project that resolves this package

## Next Steps

1. configure repository variables on self-hosted runner:
   - `UNITY_2021_3_18F1_EDITOR_PATH`
   - `UNITY_MIQ_PROJECT_PATH`
2. execute `unity-miq-2021-compat` workflow and verify artifact outputs
3. mark workflow as required status check for PR merge policy
