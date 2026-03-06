# MIQ Native Unknown-Section Policy + Warning-Code Parity (2026-03-03)

## Summary

This update aligns native C++ `.miq` loader behavior with the Unity SDK diagnostics/policy model by adding:

- unknown-section handling policy control (`Warn|Ignore|Fail`)
- normalized warning-code extraction (`warning_codes[]`)
- policy switch exposure via `avatar_tool`

The default behavior remains backward-compatible (`Warn`).

## Scope

- native avatar load contract and package model
- MIQ native loader behavior
- `avatar_tool` diagnostic CLI output
- docs (`README`, format spec, changelog)

## Detailed Changes

### 1) Native load options and package diagnostics model

Files:

- `include/animiq/avatar/avatar_package.h`
- `include/animiq/avatar/avatar_loader_facade.h`
- `src/avatar/avatar_loader_facade.cpp`

Changes:

- Added `MiqUnknownSectionPolicy`:
  - `Warn`
  - `Ignore`
  - `Fail`
- Added `AvatarPackage.warning_codes`.
- Added `AvatarLoadOptions` with `miq_unknown_section_policy`.
- Added `AvatarLoaderFacade::Load(path, options)` overload.
- Kept `Load(path)` and mapped it to default options for compatibility.

### 2) MIQ loader unknown-section policy behavior

Files:

- `src/avatar/miq_loader.h`
- `src/avatar/miq_loader.cpp`

Changes:

- Added `MiqLoader::Load(path, policy)` overload.
- `Load(path)` now delegates to `Load(path, Warn)`.
- Introduced centralized warning helpers:
  - `PushWarning(...)`
  - `AppendWarningCode(...)`
- Unknown section handling:
  - `Warn`: append warning `MIQ_UNKNOWN_SECTION` and continue.
  - `Ignore`: skip warning and continue.
  - `Fail`: terminate with `primary_error_code = MIQ_UNKNOWN_SECTION_NOT_ALLOWED`.

### 3) avatar_tool policy input + warning-code output

File:

- `tools/avatar_tool.cpp`

Changes:

- Added argument:
  - `--miq-unknown-section-policy=warn|ignore|fail`
- Switched loading path to `AvatarLoaderFacade` so policy can be passed directly.
- Added diagnostic output fields:
  - `WarningCodes`
  - `LastWarningCode`

### 4) Build linkage update

File:

- `CMakeLists.txt`

Changes:

- Updated `avatar_tool` link target:
  - from `nativecore`
  - to `animiq_core`

Reason:

- `avatar_tool` now loads directly via avatar facade/core loader path.

## Documentation Updates

Files:

- `README.md`
- `docs/formats/miq.md`
- `CHANGELOG.md`

Updates:

- documented native parity policy and warning-code contract
- documented default policy and fail-mode primary error

## Verification

Executed:

```powershell
cmake --build build --config Release --target avatar_tool
```

Result:

- PASS (`avatar_tool.exe` linked successfully after clearing lock)

Executed:

```powershell
.\build\Release\avatar_tool.exe .\build\tmp_vx\demo_mvp.miq --miq-unknown-section-policy=warn
```

Result:

- PASS
- output includes:
  - `WarningCodes`
  - `LastWarningCode`

## Compatibility Notes

- Existing callers using `AvatarLoaderFacade::Load(path)` keep default behavior (`Warn`).
- New behavior is additive; no breaking removal was introduced.
