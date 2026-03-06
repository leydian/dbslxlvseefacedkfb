# Host WPF-First Transition Report (2026-03-05)

## Summary

This update completes the host operations policy transition from dual-host blocking (`WPF + WinUI`) to a WPF-first release model.

Current contract:

- WPF path is the required release gate.
- WinUI path is opt-in diagnostics (`-IncludeWinUi`), not a default blocker.

## Why This Change

Recent rounds showed persistent WinUI XAML compiler instability (`WMC9999` / `XamlCompiler.exe`) despite preflight remediation, while WPF remained stable.

To keep delivery predictable:

- default publish/gate success now depends on WPF baseline
- WinUI diagnostics remain available without blocking the main release path

## Implemented Changes

### 1) Host publish behavior (`tools/publish_hosts.ps1`)

- Added explicit publish mode logging:
  - `HostPublishMode: WPF_ONLY` (default)
  - `HostPublishMode: WPF_PLUS_WINUI` (`-IncludeWinUi`)
- Default run now logs WinUI as optional:
  - `WinUI publish: skipped (WPF_ONLY mode; use -IncludeWinUi for optional diagnostics track)`

### 2) HostTrack pass semantics (`tools/vsfavatar_quality_gate.ps1`)

- Added WPF-first pass statuses:
  - `PASS_WPF_BASELINE`
  - `PASS_WPF_AND_WINUI`
- HostTrack DoD pass evaluation now accepts `PASS*` family, not only exact `PASS`.

### 3) Sample report parity (`tools/vsfavatar_sample_report.ps1`)

- Updated HostTrack_DoD emission to use `PASS*` family.

### 4) CI split (`.github/workflows/host-publish.yml`)

- Required job:
  - `publish-hosts-wpf`
  - validates WPF artifacts + publish report
- Optional/non-blocking job:
  - `publish-hosts-winui-diagnostics`
  - runs WinUI path for diagnostics and uploads artifacts

### 5) Documentation alignment

- Updated WPF-first operational language in `README.md`.
- Recorded policy transition in `CHANGELOG.md`.
- Updated rollup report to include policy shift context.

## Verification (Latest Local Run)

Execution evidence:

- `build/reports/host_publish_latest.txt`
  - `Host publish run: 2026-03-05T03:44:04+09:00`
  - `HostPublishMode: WPF_ONLY`
  - `WPF exe: dist/wpf/WpfHost.exe`
  - WinUI skipped as optional track
- `build/reports/vsfavatar_gate_summary.txt`
  - `Generated: 2026-03-05T03:58:23`
  - `HostTrackStatus: PASS_WPF_BASELINE`
  - `HostTrack_DoD: PASS`
  - `Overall: PASS`
- `build/reports/quality_baseline_summary.txt`
  - `Generated: 2026-03-05T03:59:18`
  - `Overall: PASS`

## Operational Impact

### Immediate

- Release gate stability increases because WPF path is no longer coupled to WinUI build failures.
- CI failures now represent core path regressions more accurately.

### Ongoing WinUI Handling

- WinUI is preserved for diagnostics and future reactivation.
- When needed:
  - run `publish_hosts.ps1 -IncludeWinUi`
  - inspect `build/reports/winui/*` artifacts

## Remaining Work

- Investigate and resolve WinUI XAML platform-unsupported path (`WMC9999`) before restoring WinUI as a blocking release gate.
