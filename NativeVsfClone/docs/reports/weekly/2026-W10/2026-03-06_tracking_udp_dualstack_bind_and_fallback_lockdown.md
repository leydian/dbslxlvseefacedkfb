# Tracking UDP Dual-Stack Bind and Webcam Fallback Lockdown (2026-03-06)

## Summary

Stabilized OSC tracking listener initialization by introducing dual-stack UDP bind with IPv4 fallback and made runtime diagnostics explicitly expose bind mode.

Also tightened fallback behavior for environments where webcam runtime is unavailable so hybrid mode does not keep misleading fallback-ready status.

## Implemented Changes

Updated:

- `host/HostCore/TrackingInputService.cs`

Key changes:

1. UDP listener bind strategy hardening

- replaced `new UdpClient(port)` with explicit listener factory:
  - first try `AddressFamily.InterNetworkV6` + `DualMode=true` + `IPv6Any` bind
  - fallback to `AddressFamily.InterNetwork` + `IPAddress.Any` bind on socket exception
- added bind-mode diagnostics field:
  - `udp6-dual` or `udp4`

2. Tracking status/diagnostics precision uplift

- startup status now includes bind mode:
  - `SourceStatus = udp-listening:{port}:{bindMode}`
  - `StatusMessage = listening:{port} ({bindMode})`
- same bind-mode detail propagated to iFacial active and fallback-ready messages.

3. Hybrid fallback lockdown when webcam runtime is unavailable

- when webcam runtime cannot be used:
  - force source contract to iFacial-only mode (`OscIfacial`, `IfacialLocked`)
  - disable auto stability tuning for hybrid path
  - publish deterministic status:
    - `SourceStatus = ifacial-active:webcam-fallback-disabled`
    - `StatusMessage = listening:{port} ({bindMode}); fallback=disabled(webcam-runtime-unavailable)`
  - clear stale webcam-unavailable error in this branch to avoid false active-source error carryover.

## Verification Summary

Executed:

```powershell
dotnet build .\host\HostCore\HostCore.csproj -c Release --nologo
dotnet build .\host\WpfHost\WpfHost.csproj -c Release --nologo
powershell -ExecutionPolicy Bypass -File .\tools\tracking_parser_fuzz_gate.ps1
powershell -ExecutionPolicy Bypass -File .\tools\release_gate_dashboard.ps1
powershell -ExecutionPolicy Bypass -File .\tools\winui_xaml_min_repro.ps1 -NoRestore
powershell -ExecutionPolicy Bypass -File .\tools\winui_blocker_triage.ps1 -NoRestore
powershell -ExecutionPolicy Bypass -File .\tools\onboarding_kpi_summary.ps1 -TelemetryPath .\build\reports\telemetry_latest.json
```

Observed:

- `HostCore` build: PASS
- `WpfHost` build: PASS
- `tracking_parser_fuzz_gate`: PASS (`Overall: PASS`)
- release dashboard remains:
  - `ReleaseCandidateWpfOnly: PASS`
  - `ReleaseCandidateFull: FAIL`
- WinUI blocker unchanged baseline:
  - `FailureClass: TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
  - `WMC9999Count: 2`
- onboarding KPI remains sample-insufficient for full gate:
  - `SessionCount: 1`
  - gate status `INSUFFICIENT_SAMPLES`

## Known Risks or Limitations

- WinUI `WMC9999` toolchain blocker is still unresolved in this environment.
- Unity XAV2 gates were not recovered in this pass and still block `ReleaseCandidateFull`.
- Onboarding KPI policy remains fail-closed for full candidate until session count reaches policy minimum.
