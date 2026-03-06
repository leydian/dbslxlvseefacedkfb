# Tracking UDP Receive Timeout Stabilization (2026-03-06)

## Summary

This patch stabilizes iFacial UDP receive behavior in `HostCore` by removing the async timeout race pattern in the receive loop and replacing it with socket timeout-based receive semantics.

Primary outcomes:

1. Eliminated potential accumulation/conflict of in-flight receive operations during timeout windows.
2. Added explicit no-packet timeout status visibility for operators.
3. Kept no-packet rebind recovery policy while clarifying status codes/messages.
4. Added explicit recovery labeling when packet flow resumes after timeout/rebind periods.

## Problem Statement

Observed field/runtime symptom:

- `tracking=on`, `source_status=ifacial-active`, but `packets=0`, `format=unknown`, and no parse progression.

Prior implementation used:

- `ReceiveAsync(token)` + `Task.WhenAny(receiveTask, Task.Delay(...))`

Timeout path continued loop execution without fully resolving/canceling the pending receive operation. Under sustained no-packet intervals this could lead to unstable receive behavior and non-deterministic recovery.

## Implementation Details

### 1) Receive loop timeout model change

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- Replaced async race-based wait (`ReceiveAsync` + `WhenAny`) with blocking socket receive:
  - `activeClient.Receive(ref remote)`
- Added `SocketException` timeout handling (`SocketError.TimedOut`) in receive loop.
- On timeout:
  - mark timeout status (once while no packet has ever arrived),
  - invoke existing no-packet rebind policy,
  - continue loop safely.

Result:

- At most one live receive attempt exists per loop iteration.
- No unresolved receive task carry-over across timeout cycles.

### 2) Socket-level receive timeout wiring

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- Applied `ReceiveTimeout = ReceiveWatchdogPollMs` on all listener bind variants:
  - IPv4 bind (`udp4`)
  - dual-stack bind (`udp6-dual`)
  - final IPv4 fallback (`udp4-final`)

Result:

- Uniform timeout behavior regardless of bind mode.

### 3) Diagnostic status refinement

File:

- `host/HostCore/TrackingInputService.cs`

Added/updated status semantics:

- no-packet timeout marker:
  - `SourceStatus = receive-timeout:<port>`
  - `StatusMessage = receive timeout:<port> waiting packet`
- rebind states renamed for clarity:
  - `receive-rebind:<port>:<bind_mode>`
  - `receive-rebind-failed`
- successful packet recovery after timeout/rebind:
  - `ifacial-active:receive-recovered`
  - status message suffix `:recovered`

Result:

- Operator can distinguish idle waiting, rebind activity, and successful recovery without external packet tools.

## Safety and Compatibility Notes

- No public API shape changes were introduced.
- Existing parse/drop diagnostics and thresholds remain intact.
- Rebind policy thresholds/cooldowns were not changed in this patch.

## Validation

Executed:

```powershell
dotnet build host/HostCore/HostCore.csproj -c Release --no-restore
dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore
```

Results:

- `HostCore`: PASS (`0 warnings`, `0 errors`)
- `WpfHost`: PASS (`0 warnings`, `0 errors`)

## Expected Operator Impact

- Fewer stuck states where receive loop appears active but packet counters do not move.
- More actionable status text during startup/no-input windows.
- Clearer observability of timeout -> rebind -> recovered transitions.
