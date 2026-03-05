# XAV2 Front-View Orientation and Material Alignment Update (2026-03-05)

## Summary

This update closes the latest XAV2 visual mismatch loop for `개인작11-3.xav2` by addressing:

1. front/back orientation mismatch on load (avatar appeared as back view by default)
2. material/texture alignment instability from fallback mapping behavior
3. limited native material interpretation (fixed tint bias, weak alpha-mode inference)

The result is:

- XAV2 now opens in front-view orientation by default in WPF host preview.
- material base texture mapping is no longer silently overwritten by first-texture fallback.
- native renderer consumes more shader-param hints (`_BaseColor/_Color`, alpha hints) instead of relying on fixed tint behavior.

## Root Causes

### A) Orientation

- Native preview world transform applied a fixed `PI` yaw rotation for all formats.
- For XAV2 assets in this pipeline, that resulted in default back-view presentation.

### B) Material/texture alignment

- In XAV2 loader, when material payload had empty `base_color_texture_name`, code assigned `textureRefs.front()`.
- This fallback could bind an unrelated texture and distort appearance consistency.

### C) Native material interpretation gap

- Native render path used mostly fixed `base_color` values and simplified alpha handling.
- `shader_params_json` emitted by Unity exporter was underused in real draw decisions.

## Changes Implemented

### 1) XAV2-specific front-view orientation

Updated:

- `src/nativecore/native_core.cpp`

Behavior:

- `preview_yaw` is now format-aware in preview transform:
  - `XAV2` -> `0.0f` (front view default)
  - others -> existing `PI` path preserved

### 2) Removed unsafe texture fallback in XAV2 loader

Updated:

- `src/avatar/xav2_loader.cpp`

Behavior:

- removed automatic assignment:
  - `payload.base_color_texture_name = texture_refs.front()` when empty
- loader now respects material payload’s explicit texture reference only.

### 3) Exporter key stability + alpha inference improvements

Updated:

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`

Behavior:

- material/texture names are emitted as stable unique keys (asset-path-based where possible).
- alpha mode/cutoff inference broadened for lilToon-like materials:
  - render queue, render tags, clip/surface/blend hints considered.

### 4) Native material param usage expansion

Updated:

- `src/nativecore/native_core.cpp`

Behavior:

- parses shader param strings to extract:
  - base color hints (`_BaseColor`, `_Color`)
  - alpha-related hints (`_AlphaClip`, `_UseAlphaClipping`, `_Cutoff`, `_Surface`, `_Mode`, alpha keywords)
- removes fixed beige tint dependency and applies material-derived color.
- blend/mask handling now better aligned with parsed material intent.

## Validation Snapshot

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release
cmake --build build --config Release
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild
```

Outcome:

- HostCore build: PASS
- nativecore build: PASS
- WPF publish/smoke: PASS

Runtime confirmation:

- user confirmed front view is now correct after re-publish.

## Files in This Update

- `src/nativecore/native_core.cpp`
- `src/avatar/xav2_loader.cpp`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
- `docs/reports/xav2_front_view_and_material_alignment_2026-03-05.md`
- `docs/reports/session_change_summary_2026-03-05.md`
- `docs/INDEX.md`
