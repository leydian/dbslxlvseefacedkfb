# 2026-03-08 VRM WPF Render Recovery and Loader Index Contract Fix

## Summary

Resolved a multi-stage WPF render failure and severe VRM visual corruption issue for `사라2.1222222.vrm`.
The incident started as `RenderRc: Internal` with `RenderReadyAvatars: 1` and `LastFrameMs: 0.000`, then transitioned to visible-but-corrupted output after temporary runtime fallbacks.

Final stabilization was achieved by restoring VRM primitive index/material contract in the loader and hardening WPF/native render recovery paths.

## Operator Symptoms

1. Initial state:
- Avatar load appeared successful (`RenderReadyAvatars: 1`)
- Render loop did not advance (`LastFrameMs: 0.000`)
- Runtime returned `RenderRc: Internal`

2. Intermediate state after emergency fallback:
- Avatar became visible
- Mesh exploded/fragmented due to unsafe non-index fallback on some primitives

3. Further state:
- Corruption reduced, but model appeared partially loaded (missing large sections)

## Root-Cause Chain

1. WPF attach/recovery path was fragile:
- Startup attach could fail before render tick loop became effective in some paths.
- Internal render failures needed stronger resize/reattach retries.

2. Native D3D11 window target recovery lacked robust resize pre-cleanup and fallback:
- `ResizeBuffers` path could leave render target incomplete under certain states.
- Hardware-only creation path could fail on some environments; WARP fallback was absent.

3. Runtime diagnostics were insufficient for fast isolation:
- Thumbnail worker returned only exit code 13 without actionable reason.

4. Critical VRM loader contract gap:
- Primitive indices/material mapping were not fully restored in mesh payload assembly.
- Runtime had to guess/fallback, producing corruption or missing sections.

## Implemented Changes

### 1) WPF host render attach/recovery hardening

- `host/WpfHost/MainWindow.xaml.cs`
  - Added `TryAttachRenderWindow()` and invoked it in:
    - `MainWindow_SourceInitialized`
    - `MainWindow_SizeChanged` when not attached
    - initialize flow after `Initialize()`
  - Ensured tick loop starts after successful init, allowing runtime recovery attempts to execute.

- `host/HostCore/HostController.cs`
  - Added render attach-on-tick retry when active avatar exists but window is detached.
  - Added internal-error recovery sequence:
    - resize retry
    - detach/reattach retry
    - render retry
  - Reduced reattach cooldown to 1s for faster recovery iterations.

### 2) Native window render target robustness

- `src/nativecore/native_core.cpp`
  - `nc_resize_window_render_target`: unbind + clear + flush before `ResizeBuffers`.
  - `nc_create_window_render_target` and thumbnail path device creation:
    - prefer D3D11 FL11.0
    - fallback to WARP if hardware device creation fails.

### 3) Render diagnostics and triage acceleration

- `host/HostCore/AvatarThumbnailWorker.cs`
  - Added per-run diagnostic log (`<output>.worker.log`) with detailed `FormatLastError()` context.
  - This made root-cause extraction deterministic during repeated repro loops.

- `src/nativecore/native_core.cpp`
  - Added detailed `SetError(...)` payloads for GPU mesh build failures (stride/input/buffer creation context).
  - Preserved deeper error messages instead of overwriting with generic wrapper text.

### 4) VRM primitive index/material contract recovery (final fix)

- `src/avatar/vrm_loader.cpp`
  - Restored primitive-level extraction/wiring in mesh payload phase:
    - `mode` read and validation (TRIANGLES path)
    - `indices` extraction when present
    - non-indexed triangle fallback generation when necessary
    - index range validation
    - primitive `material` -> `mesh_payload.material_index` mapping
  - Added warning codes for payload diagnostics:
    - `VRM_PRIMITIVE_MODE_UNSUPPORTED`
    - `VRM_INDEX_READ_FAILED`
    - `VRM_NONINDEXED_TRIANGLES_TOO_SMALL`
    - `VRM_NONINDEXED_TRIANGLES_FALLBACK`
    - `VRM_INDEX_OUT_OF_RANGE`

This loader-side contract recovery removed the need for destructive runtime guessing and restored stable visual output.

## Verification Evidence

### 1) Native + host publish

- Command:
  - `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1`
- Result:
  - PASS (multiple reruns during iterative recovery)
  - `dist/wpf/nativecore.dll` hash changed across fix stages as expected.

### 2) Target model repro via thumbnail worker

- Command pattern:
  - `WpfHost.exe --thumbnail-worker --avatar <path> --output <png> --width 256 --height 256`

- Observed progression:
  - `exit=13`, detail `failed to upload mesh payloads to GPU`
  - `exit=13`, detail `gpu mesh build invalid input ... index_count=0`
  - after loader contract fixes: `exit=0`, thumbnail emitted successfully

### 3) Runtime symptom progression

- Before fixes:
  - `RenderRc: Internal`, `LastFrameMs: 0.000`
- After attach/recovery hardening:
  - frame loop resumed but visuals still corrupted due to loader contract gap
- After loader index/material contract restoration:
  - model became renderable without fragmentation/partial-loss pattern from fallback stage

## Files Changed in This Recovery Track

- `AnimiqCore/host/WpfHost/MainWindow.xaml.cs`
- `AnimiqCore/host/HostCore/HostController.cs`
- `AnimiqCore/host/HostCore/AvatarThumbnailWorker.cs`
- `AnimiqCore/src/nativecore/native_core.cpp`
- `AnimiqCore/src/avatar/vrm_loader.cpp`

## Operational Note

This pass intentionally prioritized operator recovery speed (attach loop hardening + diagnostics) and then replaced temporary runtime fallback behavior with loader-contract correctness.
Future VRM incident response should continue to treat loader primitive contract as primary truth and runtime fallback as bounded emergency handling only.
