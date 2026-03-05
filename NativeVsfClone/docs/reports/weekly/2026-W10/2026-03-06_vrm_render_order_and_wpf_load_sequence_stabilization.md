# VRM Render Order + WPF Load Sequence Stabilization (2026-03-06)

## Summary

This update addresses the "model looks broken after load" regression as a combined rendering-policy and load-order issue.

Reference point from operator report:

- Last known-good window: around `2026-03-05 23:40` (KST)

Observed after that window:

- VRM material mode distribution changed in a way that could over-promote blend paths.
- WPF load completion and delayed render apply could race, causing camera/framing state to be re-applied at the wrong moment.

This pass keeps the conservative VRM fallback behavior and stabilizes WPF-side render apply sequencing.

## Root Causes

### 1) VRM alpha fallback regression shape

`vrm_loader.cpp` was in a state where fallback promotion behavior had recently changed.
For affected assets, this could materially alter `opaque/mask/blend` balance and surface as visible breakage.

### 2) Render apply timing around load completion

WPF uses a deferred render apply timer (`RenderApplyTimer`) for UI-driven camera/render controls.
During/near `LoadAvatar`, queued applies could still fire and overwrite the just-applied runtime state.

Net effect:

- model loads successfully, but immediately appears with unexpected framing/pose/camera relation,
- making rendering look unstable even when parser/runtime states are healthy.

## Implementation

### A) Keep conservative VRM fallback logic

Updated file:

- `src/avatar/vrm_loader.cpp`

Policy now encoded in code:

- treat `default.opaque` and `gltf.alphaMode=OPAQUE` separately,
- allow stronger promotion only from `default.opaque`,
- keep `gltf.alphaMode=OPAQUE` mostly respected (name-signal only path remains constrained).

Key markers in code:

- `opaque_from_default`
- `opaque_from_gltf`
- `should_promote_blend`

### B) Stabilize WPF load/render sequencing

Updated file:

- `host/WpfHost/MainWindow.xaml.cs`

Changes:

- added `IsLoadOperationActive()` guard,
- `RenderApplyTimer_Tick` now exits early while load is active,
- `QueueRenderApply()` no-ops while load is active,
- after successful load, force-sync render controls from controller state and refresh diagnostics view immediately.

Intent:

- ensure `ApplyRenderOptionsLoadAvatar` result remains authoritative right after load,
- avoid delayed UI apply overriding runtime state.

### C) Align host-side default yaw contract

Updated file:

- `host/HostCore/NativeCoreInterop.cs`

Change:

- `BuildBroadcastPreset().YawDeg` changed to `0.0f` (from `192.0f`).

Rationale:

- prevents immediate default orientation drift at startup/load when UI sliders and sanitized runtime range are centered around front-facing operation.

## Validation

### Build

Executed:

- `cmake --build NativeVsfClone/build --config Release --target nativecore` (pass)
- `dotnet build NativeVsfClone/host/WpfHost/WpfHost.csproj -c Release` (pass)

Notes:

- first `dotnet build` attempt failed with `NU1301` (sandbox/network restriction to `api.nuget.org`),
- rerun with elevated permission succeeded.

### Runtime expectations to verify in operator flow

Recommended smoke sequence:

1. `Initialize`
2. `Load` (`sample/개인작10-2.vrm`)
3. confirm no immediate post-load framing/camera jump
4. check runtime `MaterialModes` and `LastMaterialDiag`
5. cross-check with `avatar_tool` for same asset family

## Operational Notes

- Existing non-related untracked artifacts were intentionally not included in this update.
- This report documents implementation + verification performed on `2026-03-06` KST.

