# Session Change Summary (2026-03-06)

## Summary

This document consolidates the current workspace changes as of `2026-03-06` for:

- consumer-facing WPF UX restructuring (Beginner/Advanced mode split)
- HostCore session/guide policy updates
- tracking input plumbing interfaces and service implementation
- VSFAvatar gate script sample set and render-gate criteria expansion
- WinUI host tracking panel surface alignment

The update scope is primarily host UX/operation surface and gate/tooling contracts. Native runtime ABI behavior was only extended where tracking frame submission interop was already exposed.

## Change Set Snapshot

### Modified files

- `host/HostCore/HostController.MvpFeatures.cs`
- `host/HostCore/HostController.cs`
- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/HostUiState.cs`
- `host/HostCore/NativeCoreInterop.cs`
- `host/HostCore/PlatformFeatures.cs`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`
- `tools/vsfavatar_quality_gate.ps1`
- `tools/vsfavatar_render_gate.ps1`
- `tools/vsfavatar_sample_report.ps1`

### Added files

- `host/HostCore/TrackingInputService.cs`

## Detailed Implementation Changes

### 1) WPF consumer UX restructuring

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

Key changes:

- Added explicit UI mode controls:
  - `Beginner` mode: simplified operator path
  - `Advanced` mode: full operational surface
- Reorganized left-pane information architecture into consumer-first sections:
  - mode selection
  - quick status
  - session/avatar/render/outputs
  - advanced-only tracking and platform ops
- Localized primary UI copy to Korean-first with English technical terms in labels.
- Added diagnostics auto-reveal policy on failure events:
  - avatar load failure
  - preflight failure
  - output start failure (Spout/OSC)
  - raised runtime errors
- Added mode visibility controls in code-behind:
  - advanced section hide/show
  - diagnostics row collapse/expand
  - mode button state styling/locking

Behavioral intent:

- First-run users can complete `Initialize -> Load -> Start output` with reduced cognitive load.
- Failure situations still expose diagnostics quickly without requiring manual deep navigation.

### 2) HostCore session model and guidance updates

Updated:

- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.MvpFeatures.cs`

Key changes:

- `SessionPersistenceModel` schema extended:
  - `Version` default moved from `1` to `2`
  - added persisted `UiMode` (`beginner`/`advanced`)
- `SessionStateStore.Load()` now supports fallback normalization from a v1 shape (`SessionPersistenceModelV1`) to v2 defaults.
- Added normalization guards for persisted values:
  - default UI mode resolution
  - version floor
  - timestamp default safety
- Added `HostController` UI mode accessors:
  - `GetUiMode()`
  - `SetUiMode(string uiMode)`
- Updated import guidance strings to consumer-readable Korean wording.
- Updated quickstart/compatibility content (`HostContent`) to Korean-first operational guidance and fallback policy explanation.

### 3) HostCore tracking interfaces and interop path

Updated:

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/NativeCoreInterop.cs`
- `host/HostCore/HostController.cs`

Added:

- `host/HostCore/TrackingInputService.cs`

Key changes:

- Added tracking domain contracts:
  - `TrackingStartOptions`
  - `TrackingDiagnostics`
  - `ITrackingInputService`
- Added native interop struct and function binding:
  - `NcTrackingFrame`
  - `nc_set_tracking_frame(...)`
- Extended diagnostics snapshot schema:
  - `DiagnosticsSnapshot` now carries `TrackingDiagnostics Tracking` field for host UI consumers.
- Extended `HostController` with tracking-service dependency and diagnostics flow:
  - startup/shutdown coordination
  - per-tick tracking frame submission attempt
  - tracking-specific error message routing
  - tracking diagnostic reconciliation logs (`TrackingState`, `TrackingFormatDetected`, `TrackingParseError`)
- Implemented `TrackingInputService`:
  - UDP receive loop
  - OSC packet parse paths (`format-a`/`format-b`, bundle handling)
  - stale-frame policy
  - smoothing/recenter state management
  - diagnostics counters (`received`, `dropped`, `parse errors`, `fps`, `age`)

### 4) VSFAvatar gate script updates

Updated:

- `tools/vsfavatar_quality_gate.ps1`
- `tools/vsfavatar_sample_report.ps1`
- `tools/vsfavatar_render_gate.ps1`

Key changes:

- Extended fixed sample set with wildcard sample:
  - `*11-3.vsfavatar`
- Render gate now parses per-sample rows and adds target-sample presence checks:
  - `GateR3` target sample row presence
  - target sample metrics in summary (`stage`, `primary_error`, `mesh_payloads`)
- Added `TargetSamplePattern` parameter with default `*11-3.vsfavatar`.

### 5) WinUI host tracking panel alignment

Updated:

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Key changes:

- Added `Tracking Input` section to WinUI left pane:
  - listen port field
  - start/stop/recenter controls
  - tracking status line
- Added WinUI code-behind handlers and state wiring:
  - `StartTracking_Click`, `StopTracking_Click`, `RecenterTracking_Click`
  - enable/disable logic bound to `TrackingDiagnostics`
  - diagnostics text enrichment with tracking telemetry fields
  - session default binding for tracking listen port
- Shifted existing `Platform Ops` section row index to keep layout order deterministic.

## Validation and Current Blockers

Executed during this session:

```powershell
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release --no-restore
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore
```

Observed status:

- `HostCore`: FAIL due pre-existing/in-flight tracking contract mismatch in current workspace:
  - `TrackingDiagnostics` constructor/signature mismatch (`ParseErrors` parameter)
  - unresolved `TrackingInputService` reference in current compile graph
  - `TrackingStartOptions` constructor arity mismatch in one path
- `WpfHost`: FAIL due restore/network access issue:
  - `NU1301` to `https://api.nuget.org/...` (`api.nuget.org:443`)

Interpretation:

- This change set is documented as implementation-complete at code level for UX and policy updates, but final build green is currently blocked by workspace-level compile drift and environment-level restore/network conditions.

## Notes

- WPF is the primary consumer UX conversion target in this update; WinUI changes are limited to tracking panel alignment.
- Generated local artifacts and large sample payloads outside `NativeVsfClone/` were intentionally excluded from documentation scope and commit intent.

## Addendum: iFacialMocap Direct Path (2026-03-06)

Follow-up implementation completed in this same date window:

- switched host tracking path to direct iFacialMocap OSC ingestion defaults (`port=49983`, `stale=500ms`)
- added parser dual-mode auto-detection (`format-a` single-value, `format-b` bundle/array-like)
- connected per-tick tracking submission to native (`nc_set_tracking_frame`) before render
- expanded persisted session schema (`TrackingInputSettings`, `Version=3`)
- aligned WPF/WinUI tracking controls to manual operator flow (`start/stop/recenter`)

Updated verification snapshot for this addendum:

- `dotnet build host\\HostCore\\HostCore.csproj -c Release`: PASS
- `dotnet build host\\WpfHost\\WpfHost.csproj -c Release`: PASS
- `dotnet build host\\WinUiHost\\WinUiHost.csproj -c Release`: blocked by environment/toolchain-level WinUI XAML compiler failure (`XamlCompiler.exe` exit code 1)
