# WPF Verification Roundup (2026-03-05)

## Summary

This document consolidates the latest WPF-first verification round executed after the WPF refresh-throttle implementation and report updates.

Round objective:

- finalize current verification evidence for the WPF-first release path
- refresh documentation with exact artifact timestamps and outcomes
- keep WinUI recovery work as a separate follow-up track

## Scope and Context

Recent relevant commits before this round:

- `f7dab29` - `feat(wpf): throttle ui refresh and optimize diagnostics rendering`
- `eedeb85` - `docs(wpf): add refresh-throttle validation report and follow-up tracking`

This round did not introduce additional product-code behavior changes in `HostCore`/`WpfHost`.
It focused on evidence refresh, validation reruns, and documentation closure.

## Executed Commands

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1
```

Non-interactive launch smoke check:

- launch `dist/wpf/WpfHost.exe` with working directory set to `dist/wpf`
- verify process remains alive for 5 seconds
- force-stop process after confirmation

## Verification Results

### Host publish (`publish_hosts.ps1`)

- status: PASS
- mode: `WPF_ONLY`
- evidence:
  - `build/reports/host_publish_latest.txt`
  - `Host publish run: 2026-03-05T14:50:03.7482246+09:00`
  - WPF executable published to `dist/wpf/WpfHost.exe`

### VSFAvatar gate (`vsfavatar_quality_gate.ps1 -UseFixedSet`)

- status: PASS
- evidence:
  - `build/reports/vsfavatar_gate_summary.txt`
  - `Generated: 2026-03-05T15:12:46`
  - `HostTrackStatus: PASS_WPF_BASELINE`
  - `Overall: PASS`

### Quality baseline (`run_quality_baseline.ps1`)

- status: PASS
- evidence:
  - `build/reports/quality_baseline_summary.txt`
  - `Generated: 2026-03-05T15:13:41`
  - `Overall: PASS`

### WPF launch smoke

- first launch attempt without explicit working directory: FAIL
- second launch attempt with `dist/wpf` working directory: PASS
- interpretation:
  - executable startup path is validated in this CLI/headless round when launched under expected working directory

## Documentation Updates in This Round

- `docs/reports/wpf_ui_smoke_and_perf_2026-03-05.md`
  - replaced open `PENDING` markers with explicit status text
  - added latest pipeline verification snapshot
  - documented deferred manual-only evidence explicitly

- `docs/INDEX.md`
  - updated WPF smoke/perf report description
  - added this roundup report entry

- `CHANGELOG.md`
  - added this verification-refresh round summary with latest artifact references

## Generated Report Refreshes

The baseline rerun also refreshed generated VRM report timestamps:

- `build/reports/vrm_gate_fixed5.txt`
- `build/reports/vrm_probe_fixed5.txt`

Only timestamp headers changed; gate result semantics were not changed by this refresh.

## Deferred Items (Explicit)

Still deferred to a manual/operator-capable run:

1. full 7-step interactive WPF GUI smoke flow
2. quantitative `LastFrameMs` before/after comparison table
3. logs-tab active/inactive runtime resource impact measurements

These are intentionally separated from this CLI-centric validation round.

## Follow-up Update (2026-03-05, parity implementation round)

Additional validation rerun executed after WinUI parity code changes:

- `publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI preflight: PASS
  - WinUI publish: FAIL (`TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`, `WMC9999`/`XamlCompiler.exe`)
- `vsfavatar_quality_gate.ps1 -UseFixedSet`
  - `HostTrackStatus=PASS_WPF_BASELINE`
  - `Overall: PASS`
- `run_quality_baseline.ps1`
  - `Overall: PASS`

WPF non-interactive launch smoke result in this follow-up run:

- FAIL (`exit=-532462766`)
- this differs from the earlier same-day snapshot and is tracked as an open environment/runtime investigation item.
