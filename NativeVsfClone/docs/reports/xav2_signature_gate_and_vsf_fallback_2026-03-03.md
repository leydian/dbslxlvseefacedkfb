# XAV2 Signature Routing + Gate Expansion Report (2026-03-03)

## Summary

This update finalizes the XAV2 operational path around three goals:

1. Extension plus signature mixed routing in avatar loader dispatch.
2. End-to-end XAV2 visibility in CLI/native format surfaces.
3. VXAvatar/VXA2 quality gates expanded to include fixed and corrupt XAV2 contracts.

In parallel, the repository also includes the in-flight XAV2 payload and VSFAvatar probing extensions landed in the same working set.

## Scope

- Native loader interface and facade dispatch.
- Format name surfaces (`avatar_tool`, `vsfclone_cli`).
- Gate scripts and CI workflow naming.
- Documentation contract updates (README/docs index/changelog linkouts).

## Implemented Changes

### 1) Loader dispatch hardening (`extension -> signature fallback`)

Files:

- `include/vsfclone/avatar/i_avatar_loader.h`
- `src/avatar/avatar_loader_facade.cpp`
- `src/avatar/vrm_loader.h/.cpp`
- `src/avatar/vxavatar_loader.h/.cpp`
- `src/avatar/vxa2_loader.h/.cpp`
- `src/avatar/xav2_loader.h/.cpp`
- `src/avatar/vsfavatar_loader.h/.cpp`

Changes:

- Added `CanLoadBytes(const std::vector<std::uint8_t>& head)` to `IAvatarLoader`.
- Implemented lightweight signature checks per loader:
  - VRM: `glTF` magic
  - VXAvatar: ZIP signatures
  - VXA2: `VXA2`
  - XAV2: `XAV2`
  - VSFAvatar: `UnityFS`
- Updated `AvatarLoaderFacade::Load`:
  - route by extension first
  - if no extension match, read file header and retry by signature
  - return `unsupported file extension or signature` when both fail.

### 2) XAV2 format visibility completion in tools

Files:

- `tools/avatar_tool.cpp`
- `src/main.cpp`

Changes:

- Added `XAV2` mapping in format/source type printers.
- Ensures diagnostics and gate parsing no longer show XAV2 as `Unknown`.

### 3) VXAvatar/VXA2 gate expansion to include XAV2

Files:

- `tools/vxavatar_sample_report.ps1`
- `tools/vxavatar_quality_gate.ps1`
- `.github/workflows/vxavatar-gate.yml`

Changes:

- Sample report now supports XAV2 fixed/discovered inputs.
- Added fixed XAV2 generation fallback:
  - if no `.xav2` sample exists, invoke `vrm_to_xav2` from a VRM sample.
- Added synthetic corrupt XAV2 samples:
  - manifest-size mismatch
  - section truncation
- Added gate rules:
  - Gate F: fixed XAV2 success contract
  - Gate G: synthetic XAV2 corruption classification contract
- Gate/report header updated to `VXAvatar/VXA2/XAV2`.
- CI workflow display name updated to match expanded scope.

### 4) Documentation sync

Files:

- `README.md`
- `docs/INDEX.md`

Changes:

- Documented extension plus signature mixed routing policy.
- Updated quality gate section title/rules/synthetic sample list to include XAV2.
- Updated docs index gate report naming.

## Verification

Executed in this workspace:

1. Build
   - `cmake --build build --config Release`
   - Result: success (`vsfclone_core`, `nativecore`, `avatar_tool`, `vrm_to_xav2`, `vsfavatar_sidecar`, `vsfclone_cli`)

2. Quick gate
   - `powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`
   - Result: `GateA/B/C/D/E/F/G = PASS`

3. Signature fallback behavior
   - Renamed `demo_mvp.vxa2` to `.bin` and loaded via `avatar_tool`
   - Result: `Format: VXA2` detected successfully via signature route

## Notes

- This report documents the XAV2 routing/gate completion slice.
- The same working tree also contains broader XAV2 payload and VSFAvatar probing changes that were already in progress and are committed together in this delivery.
