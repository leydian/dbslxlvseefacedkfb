# UI Host Auto-Quality Report (2026-03-03)

## Scope

This update targets host preview quality degradation on high-DPI displays by enforcing DPI-aware render target sizing and automatic resize handling.  
The goal is to improve visible sharpness without adding manual quality controls.

Applied targets:

- `host/WpfHost`
- `host/WinUiHost`
- shared host orchestration in `host/HostCore`

## Implemented Changes

### 1) Render metric model expansion

Updated:

- `host/HostCore/HostUiState.cs`

Additions:

- `HostSessionState`
  - `LogicalWidth`, `LogicalHeight`
  - `DpiScaleX`, `DpiScaleY`
  - `RenderWidthPx`, `RenderHeightPx`
- `OutputState`
  - `SpoutWidthPx`, `SpoutHeightPx`, `SpoutFps`

Purpose:

- make effective render resolution explicit in diagnostics
- provide controller-level source of truth for automatic reconfiguration

### 2) Shared auto-quality controller logic

Updated:

- `host/HostCore/HostController.cs`

Behavior additions:

- `UpdateRenderMetrics(...)` publishes runtime render metrics to snapshot state.
- `ResizeWindow(...)` now reconfigures Spout automatically when active and target dimensions changed.
- Added resize-time operation events:
  - `SpoutAutoStop`
  - `SpoutAutoStart`
  - `SpoutAutoReconfigure`

Policy:

- no manual quality mode toggle
- automatic consistency between render target and output transport dimensions

### 3) WPF host DPI-aware sizing + debounce

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Changes:

- Physical pixel target computed via `VisualTreeHelper.GetDpi(RenderHost)`.
- Attach / resize / Spout start use physical pixel size (not logical size).
- Added resize debounce timer (~90ms) before `ResizeWindow(...)`.
- Runtime diagnostics now include:
  - logical size
  - DPI scale
  - effective render target pixel size

### 4) WinUI host parity path

Updated:

- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- Physical pixel target computed via `RenderHost.XamlRoot.RasterizationScale`.
- Attach / resize / Spout start use physical pixel size.
- Added resize debounce timer (~90ms), parity with WPF flow.
- Added same auto-quality diagnostics telemetry line.

## Verification Summary

Executed:

```powershell
dotnet build host/WpfHost/WpfHost.csproj -c Release
```

Result:

- succeeded (`0 warnings`, `0 errors`)

Executed:

```powershell
dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64
```

Result:

- failed at Windows App SDK XAML compiler stage:
  - `Microsoft.UI.Xaml.Markup.Compiler... XamlCompiler.exe ... exit code 1`

## Known Limitations

- WinUI runtime validation remains blocked by environment/toolchain XAML compile failure diagnostics.
- Render quality policy is currently fully automatic by design; no user-facing quality presets are exposed.

## Next Steps

- validate WPF visual sharpness on 125%/150% DPI monitors using new diagnostics line
- resolve WinUI XAML compiler blocker and complete parity runtime test
- optionally add render-size mismatch warning banner if physical/logical scale drift is detected at runtime
