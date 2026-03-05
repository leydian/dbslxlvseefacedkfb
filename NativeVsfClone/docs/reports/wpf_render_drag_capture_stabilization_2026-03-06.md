# WPF Render Drag Capture Stabilization (2026-03-06)

## Summary

This update hardens WPF render-view interaction stability for camera control.

Primary objective:

- keep right-drag rotation interaction stable even when the cursor temporarily moves outside the render child window during drag.

Scope:

- `host/WpfHost/RenderHwndHost.cs`

No host-state contract, preset schema, or nativecore ABI changes were introduced in this pass.

## Problem

The render interaction path in WPF used a child HWND (`RenderHwndHost`) and received mouse drag events from window messages.

Without explicit mouse capture:

- right-drag move/up delivery could become inconsistent when pointer focus left the child HWND region,
- resulting in intermittent drag discontinuity during manual camera adjustment.

## Implemented Changes

Updated:

- `host/WpfHost/RenderHwndHost.cs`

Details:

1. Right-drag start (`WM_RBUTTONDOWN`)
- sets internal drag state active
- calls `SetCapture(hwnd)` to retain mouse message ownership for the drag lifecycle
- emits existing drag-start event for host UI mapping layer

2. Drag move (`WM_MOUSEMOVE`)
- keeps existing drag-move event emission while drag state is active
- now benefits from capture-retained event continuity

3. Drag end (`WM_RBUTTONUP`)
- emits existing drag-end event
- clears drag state
- calls `ReleaseCapture()` to restore normal pointer routing

4. Event surface and behavior compatibility
- preserved existing event contracts:
  - `RenderRightDragStarted`
  - `RenderRightDragMoved`
  - `RenderRightDragCompleted`
  - `RenderMouseWheel`
- preserved existing wheel/drag semantic mapping handled in `MainWindow.xaml.cs`

## Behavioral Impact

- right-drag camera yaw interaction is more robust against pointer boundary transitions.
- no behavior change for wheel zoom path.
- no value-range or camera-mode policy changes in this pass.

## Risk and Compatibility

- low-risk, localized host-side change only.
- no persistence format impact (`render_presets.json` unchanged).
- no interop struct/API changes (`HostCore` / `nativecore` unaffected).

## Verification Snapshot

Attempted:

```powershell
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore
```

Result in current execution environment:

- build blocked by restore/network restrictions (`NU1301`, `api.nuget.org:443` access denied).

Interpretation:

- compile validation is pending in a network-enabled environment.
- static code inspection confirms event lifecycle consistency (`SetCapture` on drag start, `ReleaseCapture` on drag completion).
