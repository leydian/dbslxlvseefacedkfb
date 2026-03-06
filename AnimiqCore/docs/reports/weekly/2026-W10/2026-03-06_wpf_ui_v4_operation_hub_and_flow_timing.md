# WPF UI v4: Operation Hub + First Broadcast Flow Timing (2026-03-06)

## Summary

Implemented a fourth-stage WPF operator-flow refinement focused on reducing time-to-first-broadcast for real operators:

- added a compact operation hub with direct step actions (`Initialize`, `Load Avatar`, `Start Output`)
- surfaced fixed blocking reason text in the same primary action area
- added app-level automatic first-broadcast timing telemetry (latest + rolling median)
- reduced initial cognitive load by default-collapsing render advanced controls

This update preserves runtime API behavior and extends host diagnostics visibility.

## Scope

- Target:
  - `host/WpfHost`
  - `host/HostCore` diagnostics snapshot and UI-facing timing API
- Non-target:
  - `host/WinUiHost`
  - native render runtime contract
- Compatibility:
  - existing event handlers and core user flows remain valid

## Changed Files

- `host/HostCore/HostUiState.cs`
- `host/HostCore/HostController.cs`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

## Detailed Changes

### 1) HostCore diagnostics schema extension for UI flow timing

`DiagnosticsSnapshot` was extended with UI flow timing fields:

- `UiFlowTimingVersion`
- `FirstBroadcastStartMs`
- `FirstBroadcastStartTimestamp`

These fields are published through the existing diagnostics update path and are visible in runtime diagnostics text.

### 2) First-broadcast timing instrumentation in `HostController`

Added host-side timing lifecycle for "session start -> first successful broadcast output":

- timing starts at `Initialize` operation entry
- timing completes on first successful `StartSpout` or `StartOsc`
- timing resets on `Shutdown` or failed initialize path
- rolling sample retention (capacity: 20) with median calculation

Added UI-facing accessor:

- `GetUiFlowTimingSnapshot()`
  - `LatestMs`
  - `MedianMs`
  - `SampleCount`
  - `OutputKind`
  - `StartedTimestampUtc`

### 3) Getting Started operation hub uplift (`MainWindow.xaml`)

Enhanced top-level operator panel with:

- quick action buttons:
  - `QuickInitializeButton`
  - `QuickLoadAvatarButton`
  - `QuickStartBroadcastButton`
- fixed timing cards:
  - `FirstBroadcastTimingText`
  - `FlowMedianTimingText`
- fixed block-reason panel:
  - `ActionBlockReasonText`

The UI keeps these signals in one zone to reduce section hopping during startup operations.

### 4) WPF action/control wiring updates (`MainWindow.xaml.cs`)

- added handlers:
  - `QuickInitialize_Click`
  - `QuickLoadAvatar_Click`
  - `QuickStartBroadcast_Click`
- unified primary action output-start branch to reuse `QuickStartBroadcast_Click`
- synchronized enabled-state policy for new quick action buttons with existing `HostUiPolicy` availability
- bound onboarding block/recovery reason into persistent block-reason text panel
- displayed first-broadcast latest/median timing from `HostController` snapshot
- extended runtime diagnostics text output with new timing fields

### 5) Render advanced default visibility policy

- changed `RenderAdvancedExpander` default to collapsed (`IsExpanded=False`)
- removed automatic forced expansion on section activation
- behavior now favors fast-path controls first, while preserving advanced controls on demand

## Verification

Commands executed:

- `dotnet build NativeAnimiq\host\HostCore\HostCore.csproj -c Release --no-restore`
- `dotnet build NativeAnimiq\host\WpfHost\WpfHost.csproj -c Release --no-restore`

Results:

- HostCore: PASS (`0 warnings`, `0 errors`)
- WpfHost: PASS (`0 warnings`, `0 errors`)

Environment note:

- initial restore-required build attempts were blocked by sandboxed network policy (`NU1301`) and validated via no-restore build pass after restore access.

## Known Risks or Limitations

- first-broadcast timer currently starts from session initialize; it does not yet include app process launch timestamp.
- measurement is host-session scoped and not yet exported as standalone artifact.
- WinUI host parity for this exact operation hub/timing surface is pending.

## Next Steps

1. Add timing export (CSV/JSON) for cross-build regression dashboards.
2. Add automated gate threshold check for median first-broadcast time.
3. Port operation hub parity design to WinUI after WPF stabilization window.
