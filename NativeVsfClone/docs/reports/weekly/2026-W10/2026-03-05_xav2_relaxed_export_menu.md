# XAV2 Relaxed Export Menu Update (2026-03-05)

## Summary

This update addresses Unity SDK export failure cases triggered by strict shader policy enforcement in the XAV2 exporter.

Observed failure example:

- `XAV2 strict shader policy violation: material='All_White', shader='Standard'.`

Root cause:

- `Xav2ExportOptions.FailOnMissingShader` defaults to `true`.
- Strict allowlist contains only:
  - `lilToon`
  - `Poiyomi`
  - `potatoon`
  - `realtoon`
- Any material shader outside that set causes export failure in strict mode.

Implementation goal:

- Preserve strict policy as the default/safe path.
- Add an explicit, opt-in export path for temporary policy bypass.

## Changes Implemented

Updated file:

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2ExportMenu.cs`

### 1) Added opt-in relaxed export menu

New Unity editor menu entry:

- `Tools/VsfClone/XAV2/Export Selected AvatarRoot (Relaxed)`

Behavior:

- Uses the same export flow as strict mode.
- Sets `FailOnMissingShader = false` before invoking exporter.
- Allows exporting assets that include non-allowlisted shaders such as `Standard`.

### 2) Preserved existing strict menu behavior

Existing menu remains unchanged in policy:

- `Tools/VsfClone/XAV2/Export Selected AvatarRoot`

Behavior:

- Continues strict validation (`FailOnMissingShader = true`).
- Continues to fail on non-allowlisted shaders.

### 3) Refactored menu code into shared internal flow

Introduced internal shared method:

- `ExportSelectedAvatarRootInternal(bool relaxed)`

Benefits:

- Removes duplicate UI/export logic between strict and relaxed entries.
- Keeps policy difference isolated to one option assignment:
  - `FailOnMissingShader = !relaxed`

### 4) Mode-aware diagnostics/logging

Export completion log now includes mode:

- `[XAV2] Export complete (strict): ...`
- `[XAV2] Export complete (relaxed): ...`

Export failure log and dialog now include mode:

- `[XAV2] Export failed (strict): ...`
- `[XAV2] Export failed (relaxed): ...`
- dialog message prefix:
  - `[strict] ...`
  - `[relaxed] ...`

This makes triage easier when users switch between strict and relaxed exports.

### 5) Dialog text safety cleanup

Selection/failure dialog button/message strings were normalized to ASCII/English in this file to avoid locale-dependent mojibake in source-encoded paths.

## Public Surface / Compatibility

No runtime format/API changes:

- No `.xav2` binary format change.
- No runtime loader change.
- No native core/host interface change.

Editor-only behavior addition:

- One new menu command for relaxed export.

Compatibility impact:

- Strict path remains backward compatible and policy-consistent.
- Relaxed path is opt-in and does not alter existing strict users.

## Validation Status

Static verification performed:

- Confirmed new menu registration and validation entries exist.
- Confirmed mode-aware logs/failure messaging in diff.

Manual Unity verification still required:

1. Export avatar with `Standard` material via strict menu: expected failure.
2. Export same avatar via relaxed menu: expected success.
3. Export allowlisted shader avatar via both menus: expected success on both.

## Files in This Update

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2ExportMenu.cs`
- `docs/reports/xav2_relaxed_export_menu_2026-03-05.md`
- `docs/INDEX.md` (report link added)
