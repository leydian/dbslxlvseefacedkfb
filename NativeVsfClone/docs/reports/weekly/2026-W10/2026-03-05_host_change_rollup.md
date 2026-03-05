# Host Change Rollup (2026-03-05)

## Purpose

This document is a consolidated, operator-focused summary of the latest Host diagnostics hardening changes and their current runtime impact.

Coverage window:

- `5fabc34` docs/schema hardening pass
- `08b3fb8` post-push re-validation pass
- `067d0dd` consolidated host rollup documentation pass
- `ccaefc5` preflight-unblock and blocker-transition documentation pass

## Commit-by-Commit Breakdown

### `5fabc34` - diagnostics schema hardening

- added `preflight_probe` evidence structure in WinUI manifest
- kept legacy `preflight` contract for downstream compatibility
- documented classification precedence and evidence model

### `08b3fb8` - post-push re-validation

- re-ran host/gate/baseline after schema hardening push
- confirmed deterministic state under preflight-blocked environment
- recorded `HostTrackStatus=BLOCKED_TOOLCHAIN_PRECONDITION`

### `067d0dd` - consolidated rollup

- introduced this rollup report as the single operator-oriented summary
- linked rollup from docs index and changelog

### `ccaefc5` - preflight unblock transition

- installed Windows SDK `10.0.19041` and verified metadata probe success
- confirmed WinUI state moved from preflight-blocked to XAML compile-blocked
- updated docs to reflect new active blocker:
  - `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
  - `HostTrackStatus=BLOCKED_XAML_PLATFORM_UNSUPPORTED`

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

## Update: Remediation Outcome (2026-03-05, same day)

The planned environment remediation has now been executed:

- installed `Microsoft.WindowsSDK.10.0.19041` via winget
- metadata probe now detects:
  - `C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.19041.0\Facade\Windows.winmd`

Transition observed after rerun:

- preflight state:
  - `FAIL (MISSING_WINDOWS_SDK_19041_METADATA)` -> `PASS`
- failure class:
  - `TOOLCHAIN_PRECONDITION_FAILED` -> `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
- HostTrack:
  - `BLOCKED_TOOLCHAIN_PRECONDITION` -> `BLOCKED_XAML_PLATFORM_UNSUPPORTED`

Current status:

- the former precondition blocker is resolved
- remaining blocker is WinUI XAML compile platform-unsupported path (`WMC9999` / `XamlCompiler.exe`)

## Latest Verification Snapshot (2026-03-05, KST)

Generated artifacts from latest run sequence:

- host publish report time:
  - `2026-03-05T03:09:45+09:00`
- VSFAvatar gate summary time:
  - `2026-03-05T03:31:56`
- quality baseline summary time:
  - `2026-03-05T03:32:52`

Observed final state:

- `publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI preflight: PASS
  - WinUI publish: FAIL (`XamlCompiler.exe`, managed `WMC9999`)
  - class: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
- `vsfavatar_quality_gate.ps1 -UseFixedSet`
  - `HostTrackStatus=BLOCKED_XAML_PLATFORM_UNSUPPORTED`
- `run_quality_baseline.ps1`
  - `Overall: PASS`

## WPF-first Policy Transition (same date)

Operational policy has been switched to WPF-first to stabilize release gates:

- default host publish mode:
  - `WPF_ONLY`
- WinUI role:
  - optional diagnostics track (`-IncludeWinUi`)
- HostTrack pass contract:
  - `PASS*` family accepted (for example `PASS_WPF_BASELINE`, `PASS_WPF_AND_WINUI`)

CI policy split:

- required:
  - WPF publish job
- optional/non-blocking:
  - WinUI diagnostics publish job
