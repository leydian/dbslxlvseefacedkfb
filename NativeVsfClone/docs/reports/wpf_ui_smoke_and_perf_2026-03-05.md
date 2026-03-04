# WPF UI Smoke + Performance Evidence (2026-03-05)

## Purpose

Capture execution evidence for the WPF UI refresh-throttle change set and define the remaining manual verification scope.

Related implementation report:

- `docs/reports/ui_wpf_refresh_throttle_2026-03-05.md`

## Build Verification

Executed commands:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release
dotnet build host/WpfHost/WpfHost.csproj -c Release
```

Result:

- HostCore: PASS
- WpfHost: PASS

## Runtime Smoke Scope (Defined)

Target flow:

1. Initialize
2. Load avatar
3. Render steady-state (~30s)
4. Start/Stop Spout
5. Start/Stop OSC
6. Resize stress
7. Shutdown

## Runtime Smoke Status (Current Round)

- Interactive GUI smoke execution: PENDING
- Reason:
  - this round was finalized from CLI-centric execution; full operator-driven GUI interaction evidence is not captured in this artifact.

## Performance Evidence (Current Round)

Policy baseline introduced by code:

- render loop remains 60Hz path (`_timer` at ~16ms)
- UI diagnostics/status refresh moved to 10Hz (`_uiRefreshTimer` at 100ms)
- logs text rebuild is log-tab-aware

Quantitative runtime metrics capture status:

- `LastFrameMs` before/after table: PENDING (manual operator run required)
- Logs tab active vs inactive comparative observation: PARTIAL
  - code path confirms active-tab gating by `DiagnosticsTabControl` selection and `LogVersion` checks

## Acceptance Gate for Closure

This evidence document is considered complete when:

1. the runtime smoke flow above is executed end-to-end without blocking defects
2. a before/after `LastFrameMs` summary table is attached
3. logs-tab active/inactive resource impact observation is attached

## Notes

- No behavior change is intended for native render cadence; this round focuses on reducing managed UI refresh churn.
- WinUI parity is intentionally out of scope for this artifact and tracked separately.
