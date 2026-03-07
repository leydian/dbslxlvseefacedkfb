# 2026-03-06 - Animiq rebrand and MIQ migration execution

## Summary
- Completed a workspace-wide rename to align product identity with `Animiq`.
- Completed avatar format migration from legacy `xav2` naming to `miq` and standardized extension usage to `.miq`.
- Corrected loader/writer magic contract to `MIQ2` (4-byte) and validated expected behavior in build and smoke checks.

## Scope
- repository structure and path-level migration
- C++ native runtime and tooling names
- Unity package IDs, assembly definitions, script class/file names
- host-facing strings/default channels and validation wording
- CI workflow filenames and path references
- documentation and report filename normalization

## Implementation Details

### 1) Naming and identity unification
- Renamed legacy product prefixes:
  - `VsfClone` -> `Animiq`
  - `vsfclone` -> `animiq`
  - `VSFCLONE_` -> `ANIMIQ_`
- Migrated include namespace path:
  - `include/vsfclone/*` -> `include/animiq/*`

### 2) MIQ format migration
- Renamed source and tools related to legacy `xav2` nomenclature:
  - `src/avatar/xav2_loader.*` -> `src/avatar/Miq_loader.*`
  - `tools/vrm_to_xav2.cpp` -> `tools/vrm_to_Miq.cpp`
  - `tools/xav2_*.ps1` -> `tools/Miq_*.ps1`
  - `tools/unity_xav2_*.ps1` -> `tools/unity_Miq_*.ps1`
- Updated host/runtime-facing extension usage:
  - `.xav2` -> `.miq`

### 3) Binary signature contract fix
- `src/avatar/Miq_loader.cpp` now validates magic `MIQ2` (4-byte).
- `tools/vrm_to_Miq.cpp` now writes magic `MIQ2` (4-byte).
- This enforces intentional non-compat behavior for legacy `xav2` artifacts unless migrated/re-exported.

### 4) Unity package migration
- Package path moved:
  - `unity/Packages/com.vsfclone.xav2`
  - -> `unity/Packages/com.animiq.miq`
- Assembly/script identifiers aligned:
  - `VsfClone.Xav2.*` -> `Animiq.Miq.*`
  - `Xav2*` -> `Miq*`

### 5) CI/workflow alignment
- Updated workflow path references to current top-level workspace path (`AnimiqCore`).
- Unified Unity workflow naming to `unity-miq-compat.yml`.

## Verification
- Build checks:
  - `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore`: PASS
  - `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore`: PASS
  - `dotnet build host/WinUiHost/WinUiHost.csproj -c Release --no-restore`: FAIL (`MSB3073` / `XamlCompiler.exe`, pre-existing baseline)
  - `cmake -S . -B build`: PASS
  - `cmake --build build --config Release --target nativecore`: PASS
  - `cmake --build build --config Release --target vrm_to_miq animiq_cli`: PASS
  - `cmake --build build --config Release --target avatar_tool`: PASS
- Runtime smoke:
  - Running updated `avatar_tool` against legacy `.xav2` sample reported:
    - `Format: MIQ`
    - `Compat: failed`
    - `PrimaryError: MIQ_SCHEMA_INVALID`
  - Result is expected after hard magic switch to `MIQ2`.

## Risks / Follow-ups
- WinUI build baseline issue remains independent from this rebrand migration and should be tracked separately.
- If backward compatibility for legacy `.xav2` is required, add explicit reader fallback path instead of weakening `MIQ2` contract.
