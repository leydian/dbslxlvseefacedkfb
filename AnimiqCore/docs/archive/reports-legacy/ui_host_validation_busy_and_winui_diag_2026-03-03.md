# UI Host Validation/Busy State + WinUI Diagnostic Env Snapshot (2026-03-03)

## Summary

This update strengthens operator safety and runtime feedback in both host shells by:

- adding shared HostCore busy/operation state
- adding shared input validation contracts for avatar/OSC inputs
- applying pre-submit button gating and inline validation messages in WPF and WinUI
- adding WinUI diagnostic environment snapshot fields in publish diagnostics manifest

## Goals and acceptance

Target operator improvements:

- prevent invalid runtime action order before user clicks
- reduce double-execution risk while host commands are in flight
- keep WPF/WinUI UX behavior aligned for operation-critical controls
- preserve better failure triage metadata for WinUI publish failures

Acceptance criteria covered in this change:

- `Load` is blocked unless avatar input is valid.
- `Start OSC` is blocked unless port/address inputs are valid.
- disruptive/start actions are blocked while a host operation is in progress.
- status strip exposes current busy operation (or idle state).
- WinUI diagnostics manifest contains reproducibility environment fields.

## HostCore changes

- Added `HostOperationState` and `HostValidationState` records.
- Added controller-level input validation API:
  - `HostValidationState ValidateInputs(string avatarPath, string oscBindPortText, string oscPublishAddress)`
- Added operation-state transitions around mutating controller operations:
  - initialize/shutdown, window attach/resize, avatar load/unload, Spout start/stop, OSC start/stop, render apply actions.

Validation rules introduced:

- avatar path:
  - non-empty
  - extension in `{.vrm, .vxavatar, .vsfavatar, .vxa2}`
  - file exists
- OSC bind port:
  - parseable `ushort` (`0..65535`)
- OSC publish address:
  - non-empty `host:port` shape
  - port parseable as `ushort`

## WPF host changes

- Added real-time `TextChanged` validation hooks for:
  - avatar path
  - OSC bind port
  - OSC publish address
- Added inline validation text blocks under related inputs.
- Added busy state field in bottom status strip.
- Updated action enable rules to include:
  - session/lifecycle state
  - validation success (for `Load`, `Start OSC`)
  - not-busy requirement
- Updated logs textbox to include horizontal scrolling for long lines.

UI behavior updates:

- busy guard added to:
  - initialize, shutdown, load, unload, start/stop spout, start/stop osc
- validation refresh occurs on:
  - text change
  - full UI refresh pass
- status strip now includes:
  - `Busy: <operation>|Idle`

## WinUI host changes

- Added left panel `ScrollViewer` to prevent control clipping on small window heights.
- Added same real-time validation hooks and inline validation text as WPF.
- Added busy status text line in status panel.
- Updated action enable rules with the same validation + not-busy gates.
- Updated logs textbox to `NoWrap` with horizontal scrolling.

UI behavior parity updates:

- same busy guards as WPF for lifecycle/output actions
- same validation refresh strategy (`TextChanged` + refresh pass)
- same gate composition (`session state + avatar presence + validation + not busy`)

## WinUI diagnostics/publish script changes

- `tools/publish_hosts.ps1` diagnostic manifest now includes an `environment` section with:
  - OS version
  - `dotnet --list-sdks`
  - `dotnet --list-runtimes`
  - Visual Studio discovery via `vswhere` (if available)

Manifest additions (schema delta):

- `environment.os_version`
- `environment.dotnet_sdks[]`
- `environment.dotnet_runtimes[]`
- `environment.visual_studio[]`

## Files touched

- `host/HostCore/HostUiState.cs`
- `host/HostCore/HostController.cs`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`
- `tools/publish_hosts.ps1`
- `docs/INDEX.md`

## Manual smoke checklist (post-network/tooling)

For both WPF and WinUI:

1. Start host and confirm `Busy: Idle`.
2. Enter invalid avatar path and verify `Load` stays disabled.
3. Enter valid avatar path and verify `Load` enables.
4. Enter invalid OSC bind port/address and verify `Start OSC` stays disabled.
5. Enter valid OSC values and verify `Start OSC` enables (when avatar loaded).
6. Execute `Initialize -> Load -> Start Spout/OSC -> Stop -> Unload -> Shutdown`.
7. During each operation, verify action buttons are temporarily gated.

## Verification

- `dotnet build host/HostCore/HostCore.csproj -c Release`: PASS
- `dotnet build host/WpfHost/WpfHost.csproj ...`: BLOCKED in this environment by NuGet network access (`NU1301`/`NU1101`)
- `dotnet build host/WinUiHost/WinUiHost.csproj ...`: BLOCKED in this environment by NuGet network access (`NU1301`/`NU1101`)
