# Automation Workflow and Spout2 Receiver MVP (2026-03-08)

## Summary

Implemented the agreed benchmark-to-productization slice in Animiq:

- core workflow runtime (`trigger -> action`) in HostCore,
- WPF operator surface for workflow JSON + visual preview,
- avatar swap and output toggling callable from workflow actions,
- new native/core API contract for Spout2 receiver control + diagnostics.

Scope delivered in this pass is MVP-first:

- execution-ready runtime path is implemented end-to-end,
- visual node editor is represented as JSON graph + in-app canvas preview (not full drag/drop authoring yet).

## Implementation Details

### 1) HostCore workflow engine and graph model

Added a new workflow domain under HostCore:

- `host/HostCore/AutomationWorkflow.cs`
  - graph schema:
    - `WorkflowGraphModel`
    - `WorkflowNodeModel`
    - `WorkflowEdgeModel`
  - trigger kinds:
    - `TimerTrigger`
    - `StateTrigger`
    - `OscTrigger`
  - action kinds:
    - `SetExpressionAction`
    - `SwapAvatarAction`
    - `SetRenderProfileAction`
    - `SetOutputStateAction`
    - `SetSpout2ReceiverStateAction`
    - `DelayAction`
  - runtime helpers:
    - `WorkflowEngine`
    - `WorkflowStore`
    - `WorkflowOscListener`

Runtime behavior:

- supports timer/state/OSC trigger firing,
- supports chained action execution via directed edges,
- supports delayed continuation via `DelayAction`,
- includes loop prevention (`visited` guard),
- includes persistent graph storage under local app data.

### 2) HostController automation integration

Added automation bridge in:

- `host/HostCore/HostController.Automation.cs`

Key capabilities:

- graph lifecycle:
  - `GetAutomationGraph()`
  - `GetAutomationGraphJson()`
  - `SetAutomationGraphJson(...)`
  - `SetAutomationEnabled(...)`
  - `GetAutomationSnapshot()`
- runtime invocation:
  - `TickAutomation()`
- action routing:
  - expression weight set (`nc_set_expression_weights`)
  - avatar swap (`LoadAvatar`)
  - render profile apply (`ApplyRenderProfile`)
  - output state control (`StartSpout/StopSpout`, `StartOsc/StopOsc`)
  - spout2 receiver state (`StartSpoutReceiver/StopSpoutReceiver`)

### 3) Spout2 receiver native/api contract extension

Added native C API surface:

- `include/animiq/nativecore/api.h`
  - `NcSpoutReceiverOptions`
  - `NcSpoutReceiverDiagnostics`
  - `nc_start_spout_receiver(...)`
  - `nc_stop_spout_receiver(...)`
  - `nc_get_spout_receiver_diagnostics(...)`

Added managed interop:

- `host/HostCore/NativeCoreInterop.cs`
  - matching structs and DllImport bindings.

Added native runtime plumbing:

- `src/nativecore/native_core.cpp`
  - receiver state fields in `CoreState`,
  - lifecycle reset on shutdown path,
  - start/stop receiver API handlers,
  - receiver diagnostics API handler.

Current receiver implementation is API-valid state/diagnostics plumbing intended for immediate HostCore integration and iterative backend expansion.

### 4) WPF UI integration

Updated:

- `host/WpfHost/MainWindow.xaml`
  - added workflow section under operations panel:
    - load/apply buttons,
    - automation enable toggle,
    - JSON editor textbox,
    - preview canvas,
    - status text.

- `host/WpfHost/MainWindow.xaml.cs`
  - added handlers:
    - `AutomationLoad_Click`
    - `AutomationApply_Click`
    - `AutomationEnabled_Changed`
  - added refresh/render helpers:
    - `RefreshAutomationUi()`
    - `RenderAutomationPreview(...)`
  - integrated runtime ticking:
    - `_controller.TickAutomation()` call in `Timer_Tick`.

## Build and Verification

Executed verification commands:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release --no-restore
dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore
cmake --build build --config Release --target nativecore
```

Results:

- HostCore build: PASS (one transient file-lock retry warning observed)
- WpfHost build: PASS
- nativecore build: PASS

## Follow-up (v1.1)

- upgrade WPF automation UI from JSON+preview to full drag/connect node editing,
- add richer OSC trigger filtering (address wildcard + payload conditions),
- wire receiver diagnostics into runtime panel parity with sender diagnostics,
- mirror this feature lane into WinUI after HostCore contract freeze.
