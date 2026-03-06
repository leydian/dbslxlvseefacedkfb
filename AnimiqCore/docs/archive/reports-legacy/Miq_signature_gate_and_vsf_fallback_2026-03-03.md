# MIQ Signature Routing + Gate Expansion Report (2026-03-03)

## Summary

This update finalizes the MIQ operational path around three goals:

1. Extension plus signature mixed routing in avatar loader dispatch.
2. End-to-end MIQ visibility in CLI/native format surfaces.
3. VXAvatar/VXA2 quality gates expanded to include fixed and corrupt MIQ contracts.

In parallel, the repository also includes the in-flight MIQ payload and VSFAvatar probing extensions landed in the same working set.

## Scope

- Native loader interface and facade dispatch.
- Format name surfaces (`avatar_tool`, `animiq_cli`).
- Gate scripts and CI workflow naming.
- Documentation contract updates (README/docs index/changelog linkouts).

## Implemented Changes

### 1) Loader dispatch hardening (`extension -> signature fallback`)

Files:

- `include/animiq/avatar/i_avatar_loader.h`
- `src/avatar/avatar_loader_facade.cpp`
- `src/avatar/vrm_loader.h/.cpp`
- `src/avatar/vxavatar_loader.h/.cpp`
- `src/avatar/vxa2_loader.h/.cpp`
- `src/avatar/miq_loader.h/.cpp`
- `src/avatar/vsfavatar_loader.h/.cpp`

Changes:

- Added `CanLoadBytes(const std::vector<std::uint8_t>& head)` to `IAvatarLoader`.
- Implemented lightweight signature checks per loader:
  - VRM: `glTF` magic
  - VXAvatar: ZIP signatures
  - VXA2: `VXA2`
  - MIQ: `MIQ`
  - VSFAvatar: `UnityFS`
- Updated `AvatarLoaderFacade::Load`:
  - route by extension first
  - if no extension match, read file header and retry by signature
  - return `unsupported file extension or signature` when both fail.

### 2) MIQ format visibility completion in tools

Files:

- `tools/avatar_tool.cpp`
- `src/main.cpp`

Changes:

- Added `MIQ` mapping in format/source type printers.
- Ensures diagnostics and gate parsing no longer show MIQ as `Unknown`.

### 3) VXAvatar/VXA2 gate expansion to include MIQ

Files:

- `tools/vxavatar_sample_report.ps1`
- `tools/vxavatar_quality_gate.ps1`
- `.github/workflows/vxavatar-gate.yml`

Changes:

- Sample report now supports MIQ fixed/discovered inputs.
- Added fixed MIQ generation fallback:
  - if no `.miq` sample exists, invoke `vrm_to_miq` from a VRM sample.
- Added synthetic corrupt MIQ samples:
  - manifest-size mismatch
  - section truncation
- Added gate rules:
  - Gate F: fixed MIQ success contract
  - Gate G: synthetic MIQ corruption classification contract
- Gate/report header updated to `VXAvatar/VXA2/MIQ`.
- CI workflow display name updated to match expanded scope.

### 4) Documentation sync

Files:

- `README.md`
- `docs/INDEX.md`

Changes:

- Documented extension plus signature mixed routing policy.
- Updated quality gate section title/rules/synthetic sample list to include MIQ.
- Updated docs index gate report naming.

## Verification

Executed in this workspace:

1. Build
   - `cmake --build build --config Release`
   - Result: success (`animiq_core`, `nativecore`, `avatar_tool`, `vrm_to_miq`, `vsfavatar_sidecar`, `animiq_cli`)

2. Quick gate
   - `powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`
   - Result: `GateA/B/C/D/E/F/G = PASS`

3. Signature fallback behavior
   - Renamed `demo_mvp.vxa2` to `.bin` and loaded via `avatar_tool`
   - Result: `Format: VXA2` detected successfully via signature route

## Notes

- This report documents the MIQ routing/gate completion slice.
- The same working tree also contains broader MIQ payload and VSFAvatar probing changes that were already in progress and are committed together in this delivery.
