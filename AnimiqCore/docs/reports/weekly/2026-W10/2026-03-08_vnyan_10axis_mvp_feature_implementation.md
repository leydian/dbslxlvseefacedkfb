# 2026-03-08 VNyan 10-Axis MVP Feature Implementation

## Summary

This update documents the implementation slice that productized VNyan benchmark directions into Animiq runtime features.

Implemented in this round:

- workflow automation trigger/action expansion,
- command-based trigger path,
- Spout receiver auto-reconnect option,
- WPF operations controls for quick presets / panic stop / recovery,
- render overlay item runtime path,
- automation extension registry contract.

## Detailed Implementation

### 1) Automation workflow expansion

`host/HostCore/AutomationWorkflow.cs`

- Added trigger kind:
  - `CommandTrigger`
- Added action kinds:
  - `SendOscAction`
  - `SetExpressionBatchAction`
  - `SpawnOverlayItemAction`
  - `ClearOverlayItemsAction`
  - `ExtensionAction`
- Added graph validation on load:
  - required nodes/edges check,
  - duplicate node id check,
  - edge source/target existence check.
- Added in-engine command queue:
  - `EnqueueCommand(...)`
  - command drain/evaluation during tick.

### 2) Host automation routing and runtime behavior

`host/HostCore/HostController.Automation.cs`

- Added command trigger entrypoint:
  - `TriggerAutomationCommand(...)`
- Added Spout receiver auto-reconnect state and tick handler.
- Extended action routing:
  - OSC send action,
  - expression batch action,
  - overlay spawn/clear enqueue actions,
  - extension action dispatch.
- Extended avatar swap action options:
  - `pre_delay_ms`
  - `post_delay_ms`
  - `fallback_path`
  - `preserve_outputs`

### 3) Automation extension contract

`host/HostCore/AutomationExtensions.cs` (new)

- Added extension interface:
  - `IAutomationExtension`
- Added registry:
  - `AutomationExtensionRegistry`

This is an MVP contract-only extension lane for registering and invoking custom actions.

### 4) WPF operations/runtime UX additions

`host/WpfHost/MainWindow.xaml`
`host/WpfHost/MainWindow.xaml.cs`

- Added automation control row:
  - command input + fire,
  - template selector + insert button.
- Added operations actions:
  - `Quick Preset: Streaming`
  - `Quick Preset: TrackingStable`
  - `Quick Preset: LowLatency`
  - `Panic Stop`
  - `Reset Tracking`
  - `Reset Expressions`
  - `Restart Outputs`
- Added render overlay canvas (`RuntimeOverlayCanvas`) and per-tick overlay lifecycle handling.
- Added global shortcut:
  - `Ctrl+Shift+X` -> panic stop.
- Extended automation status text to include receiver auto-reconnect state.

## Verification

Executed:

- `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore`
- `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore`

Observed:

- HostCore build: PASS
- WpfHost build: PASS

Note:

- one transient `CS2012` file-lock occurred during parallel build attempt and was resolved by sequential rerun.
