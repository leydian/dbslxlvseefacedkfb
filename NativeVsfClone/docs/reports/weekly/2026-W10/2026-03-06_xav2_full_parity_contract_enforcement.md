# XAV2 Full Parity Contract Enforcement (2026-03-06)

## Summary

This update enforces a strict parity contract for XAV2 material loading across Unity runtime and native loader paths.

Key outcomes:

- parity target shader families are now fixed to `liltoon` and `poiyomi`
- material payloads are canonicalized to `typed-v3` during load (auto-migration)
- unresolved typed texture refs now fail the load instead of warning-only fallback
- parity/migration diagnostics were expanded for downstream gate and tooling visibility

## Scope

- Unity runtime loader/data model/tests
- Unity exporter-side material extraction defaults (Poiyomi typed path)
- native XAV2 loader contract alignment
- package documentation updates

Out of scope:

- full shader lighting model parity implementation (multi-pass/keyword matrix fine details)
- snapshot corpus expansion and full Unity batch test execution in this shell

## Implementation Details

### 1) Unity runtime parity contract hardening

Updated:

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`

Changes:

- Added new load error: `ParityContractViolation`.
- Added diagnostics fields:
  - `MigrationApplied`
  - `SourceFormatVersion`
  - `SourceMaterialParamEncoding`
  - `CriticalParityViolation`
- Added material canonicalization stage before compatibility evaluation:
  - normalizes/infers shader family
  - enforces parity families (`liltoon`, `poiyomi`) only
  - upgrades legacy/typed-v2 material payloads to canonical `typed-v3`
  - injects required `_BaseColor` typed color when absent
  - maps base texture to typed `base` slot when possible
- Promoted unresolved typed texture refs from warning path to hard failure (`ParityContractViolation`).

### 2) Unity extraction/export defaults alignment

Updated:

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2ExportOptions.cs`

Changes:

- Poiyomi now emits typed payload baseline (`typed-v3`) like lilToon path.
- Shader family resolver now maps `Poiyomi` -> `poiyomi`.
- Strict shader set default trimmed to:
  - `lilToon`
  - `Poiyomi`

### 3) Native loader contract alignment

Updated:

- `src/avatar/xav2_loader.cpp`

Changes:

- Added parity-family guard (`liltoon`, `poiyomi`) with load failure on non-allowed family.
- Added legacy family inference from shader name (`liltoon` / `poiyomi`).
- Canonicalized material encoding to `typed-v3` and enforced minimal typed baseline (`_BaseColor`, optional base texture slot mapping).
- Promoted unresolved typed texture refs to hard failure path (`XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED` as primary error).

### 4) Runtime test expectation updates

Updated:

- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

Changes:

- typed-v2 parse expectation now validates migration to `typed-v3`.
- unsupported typed shader family expectation changed from warning to fail (`ParityContractViolation`).
- legacy material format parse test now also validates migration signal + canonical encoding.

## Verification

Executed:

```powershell
cmake --build NativeVsfClone/build --config Release --target nativecore avatar_tool
NativeVsfClone/build/Release/avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.xav2"
```

Observed:

- native build targets (`nativecore`, `avatar_tool`) passed.
- sample load now fails in payload stage with strict parity contract signal:
  - `PrimaryError: XAV2_MATERIAL_SHADER_FAMILY_NOT_ALLOWED`

## Risks / Follow-ups

- Existing assets containing non-parity families (legacy/potatoon/realtoon) now fail by default and require re-export or conversion to parity target families.
- Unity EditMode test runner execution was not performed in this shell-only environment.
- Next parity slices should focus on renderer-level high-fidelity behavior (lighting/pass/keyword semantics) under snapshot gate.
