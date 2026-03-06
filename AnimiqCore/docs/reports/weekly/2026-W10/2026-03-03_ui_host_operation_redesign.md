# UI Host Operation Redesign Report (2026-03-03)

## Scope

This update upgrades both host shells (WPF + WinUI) from MVP control panels to operation-ready UI surfaces with:

- explicit lifecycle sections (`Session`, `Avatar`, `Outputs`)
- state-based action gating to prevent invalid operation order
- structured diagnostics (`Runtime`, `Avatar`, `Logs`)
- persistent status strip for key runtime signals
- shared HostCore state/controller model for behavior parity

Goal:

`reduce operator mistakes + improve runtime visibility + keep WPF/WinUI behavior aligned`

## Implemented Changes

### 1) Shared host state/controller layer

Added:

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/HostUiState.cs`
- `host/HostCore/HostController.cs`

Design notes:

- Existing service classes remain in place and now implement interface contracts.
- New `HostController` centralizes:
  - lifecycle calls
  - render tick and avatar info refresh
  - output start/stop orchestration
  - operation log collection (bounded ring buffer, 200 entries)
  - snapshot publication and error event emission
- UI shells subscribe to controller events instead of duplicating workflow logic.

### 2) WPF host UX redesign

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

Key behavior changes:

- Added `Shutdown` action and guarded reinitialize path.
- Added avatar file browse dialog (OpenFileDialog).
- Added OSC input validation (`ushort` bind port).
- Added confirmation gates for disruptive actions:
  - reinitialize
  - unload avatar
  - stop spout/osc
  - shutdown
- Added state-driven button availability:
  - actions enabled only in valid lifecycle states
- Added diagnostics tabs:
  - Runtime: frame/runtime state
  - Avatar: parser/payload/render diagnostics
  - Logs: operation/error log stream + copy action
- Added bottom status strip:
  - session state
  - avatar loaded state
  - render return code
  - frame timing
  - output toggles
  - last error text

### 3) WinUI host UX redesign with parity target

Updated:

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Key behavior changes:

- Applied same high-level section model and action semantics as WPF.
- Added file picker browse flow (`FileOpenPicker` + window init).
- Added confirmation dialogs for destructive/restart actions.
- Added OSC bind port validation and output restart handling.
- Added runtime/avatar/log diagnostics panels and status strip.
- Added render host resize path using shared controller.

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

- failed at XAML compile stage:
  - `Microsoft.UI.Xaml.Markup.Compiler... XamlCompiler.exe ... exit code 1`
- generated `obj/.../output.json` did not include actionable line-level diagnostics.

## Known Limitations

- WinUI build verification is currently blocked by environment/toolchain-level XAML compiler failure details not being surfaced.
- WPF/WinUI functional parity was implemented at code level, but full WinUI runtime validation remains pending until the XAML compile blocker is resolved.

## Next Steps

- add a CI/diagnostic step that captures full WinUI XAML compiler diagnostics (including stderr artifacts) when compilation fails
- run manual end-to-end UX validation on both hosts once WinUI compiler blocker is resolved
- optionally split host-shell view logic into dedicated ViewModel classes if further UI complexity is planned
