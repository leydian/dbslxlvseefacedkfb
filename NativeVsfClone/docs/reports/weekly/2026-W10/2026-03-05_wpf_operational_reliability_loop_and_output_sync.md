# WPF Operational Reliability Loop and Output Sync Report (2026-03-05)

## Summary

This pass implements the WPF-first reliability plan with two concrete deliverables:

- runtime output-state mismatch detection + bounded auto-recovery in HostCore
- repeatable WPF reliability loop automation script for pre-release gating

WinUI remains a diagnostics-only track and was not promoted to release-gating criteria in this change.

## Implemented Changes

### 1) HostCore output-state reconciliation

Updated:

- `host/HostCore/HostController.cs`

Behavior additions:

- Tracks desired output intent separately from runtime-observed state:
  - `_desiredSpoutActive`
  - `_desiredOscActive`
- During `Tick(...)`, queries runtime stats and compares:
  - UI/host state (`Outputs.SpoutActive`, `Outputs.OscActive`)
  - runtime state (`nc_get_runtime_stats`)
- On mismatch:
  - writes throttled diagnostic log entry (`OutputStateSync`)
  - synchronizes host state to runtime-observed values
  - attempts bounded recovery (throttled) toward desired state:
    - Spout restart/stop using last-known channel/size/fps
    - OSC restart/stop using last-known bind/publish settings
  - writes recovery outcome entries (`OutputStateSyncSpout*`, `OutputStateSyncOsc*`)

Design intent:

- prevent silent drift where UI says output is on/off but runtime differs
- keep operator feedback and recovery action deterministic during long sessions

### 2) WPF reliability loop gate script

Added:

- `tools/wpf_reliability_gate.ps1`

Script behavior:

- Runs `publish_hosts.ps1` in WPF-first mode for N iterations.
- Requires WPF launch smoke to pass each loop (`WpfLaunchSmokeFailOnError=true`).
- Optionally runs `run_quality_baseline.ps1` at end of loop.
- Produces a single aggregate report:
  - `build/reports/wpf_reliability_gate_latest.txt`

Key options:

- `-Iterations` (default `3`)
- `-SkipNativeBuild`
- `-RunQualityBaselineAtEnd`
- `-StopOnFailure`

## Validation

Executed:

```powershell
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release
powershell -NoProfile -Command "[void][ScriptBlock]::Create((Get-Content -Raw 'NativeVsfClone\tools\wpf_reliability_gate.ps1')); 'OK'"
```

Outcome:

- `HostCore` build: PASS
- `WpfHost` build: PASS
- `wpf_reliability_gate.ps1` parse check: PASS (`OK`)

## Documentation Updates

- `README.md`:
  - added HostCore output-state reconciliation note in implemented host capabilities
  - added WPF reliability loop command + artifact output section
