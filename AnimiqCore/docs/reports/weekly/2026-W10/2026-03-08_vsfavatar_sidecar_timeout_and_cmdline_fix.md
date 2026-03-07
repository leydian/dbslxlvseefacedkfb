# VSFAvatar Sidecar Timeout + Cmdline Fix (2026-03-08)

## Summary

Resolved a high-impact false-unsupported path for `.vsfavatar` where load could fail with:
- `ERR_LOADAVATAR_UNSUPPORTED`
- `parser_stage=complete`
- `primary_error=AVATAR_RENDER_READY_MESH_PAYLOAD_MISSING`
- `mesh_payloads=0`

The failure was caused by sidecar execution instability (timeout/fallback and command-line handoff issue), not by extension-level support.

## Root Cause

1) Sidecar timeout default was too aggressive for large VSFAvatar samples.  
- Fallback path moved to inhouse parse and produced no render mesh payloads.
- Contract then failed with `AVATAR_RENDER_READY_MESH_PAYLOAD_MISSING`.

2) `CreateProcessA` command-line buffer missed explicit null terminator in `RunSidecar(...)`.  
- This could lead to sidecar invocation instability and incorrect argument handling.

## Changes

### Loader runtime hardening

File: `src/avatar/vsfavatar_loader.cpp`

- Default sidecar timeout changed to `60000ms` when `VSF_SIDECAR_TIMEOUT_MS` is not set.
- Kept environment override precedence unchanged (`VSF_SIDECAR_TIMEOUT_MS` still wins).
- Restored explicit null termination for `CreateProcessA` command buffer:
  - `cmd_mutable.push_back('\0');`

### Documentation

File: `CHANGELOG.md`
- Added 2026-03-08 hotfix entry documenting symptom, cause, and timeout policy update.

## Verification

### Repro sample
- `D:\dbslxlvseefacedkfb\sample\NewOnYou.vsfavatar`

### Before (observed)
- `W_FALLBACK: sidecar fallback: SIDECAR_TIMEOUT ...`
- final `PrimaryError: AVATAR_RENDER_READY_MESH_PAYLOAD_MISSING`
- `MeshPayloads: 0`

### After
- `avatar_tool` result:
  - `Compat: full`
  - `ParserStage: complete`
  - `PrimaryError: NONE`
  - `MeshPayloads: 1`
  - `MaterialPayloads: 1`
- `publish_hosts.ps1` rerun:
  - WPF dist refresh complete
  - launch smoke PASS

## Operational Note

If a specific `.vsfavatar` still fails, check whether it is a genuine parse limitation (`failed-serialized`, `DATA_BLOCK_*`) versus runtime invocation failure. This hotfix removes the common false-unsupported path caused by sidecar invocation budget/command-line handling.
