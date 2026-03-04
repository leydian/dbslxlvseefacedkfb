# Host Change Rollup (2026-03-05)

## Purpose

This document is a consolidated, operator-focused summary of the latest Host diagnostics hardening changes and their current runtime impact.

Coverage window:

- `5fabc34` docs/schema hardening pass
- `08b3fb8` post-push re-validation pass

## What Changed

### 1) WinUI diagnostics manifest became decision-complete

- Added `preflight_probe` to `build/reports/winui/winui_diagnostic_manifest.json`.
- Retained `preflight` legacy contract unchanged:
  - `passed`
  - `failed_checks`
  - `detected_sdks`
  - `recommended_actions`
- Added explicit probe evidence per check:
  - .NET 8 SDK detection
  - Visual Studio discovery detection
  - Windows SDK 19041 metadata path checks

### 2) Failure classification precedence was hardened

Effective class priority:

1. precondition failure (`TOOLCHAIN_PRECONDITION_FAILED`, plus `TOOLCHAIN_MISSING_DOTNET8`)
2. managed platform-unsupported (`TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`, `WMC9999`)
3. diagnostics-derived classes (`NUGET_SOURCE_UNREACHABLE`, `XAML_COMPILER_EXEC_FAIL`, etc.)
4. `UNKNOWN`

Result:

- classification no longer drifts when logs contain multiple overlapping error signatures.

### 3) HostTrack auto-resolution remained aligned

`vsfavatar_quality_gate.ps1` continues resolving HostTrack from manifest `failure_class`:

- current state: `BLOCKED_TOOLCHAIN_PRECONDITION`
- reason source: `build/reports/winui/winui_diagnostic_manifest.json`

## Current Verified State (latest run)

Environment and command-level outcomes:

- `dotnet --version`: `8.0.418` (from repo root, `global.json` pin active)
- `publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI preflight: FAIL
  - failed check: `MISSING_WINDOWS_SDK_19041_METADATA`
  - failure class: `TOOLCHAIN_PRECONDITION_FAILED`
- `vsfavatar_quality_gate.ps1 -UseFixedSet`
  - ParserTrack_DoD: PASS
  - HostTrackStatus: `BLOCKED_TOOLCHAIN_PRECONDITION`
- `run_quality_baseline.ps1`
  - Overall: PASS

## Blocker and Risk

Primary blocker:

- missing Windows SDK metadata/facade components required for WinUI net8 toolchain preflight.

Operational impact:

- WinUI publish pipeline remains blocked before parity smoke phase.
- Parser and quality gates are healthy; regression risk is concentrated in host toolchain readiness, not parser logic.

## Next Action to Unblock

Install Windows SDK 10.0.19041 metadata/facade components, then re-run:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet
```

Success condition:

- preflight check `MISSING_WINDOWS_SDK_19041_METADATA` disappears and HostTrack transitions away from `BLOCKED_TOOLCHAIN_PRECONDITION`.
