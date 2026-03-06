# R01-R20 MVP Implementation Round Report (2026-03-05)

## Scope

Implemented the previously planned `R01-R20` requirement set as functional MVP code, focusing on:

- `host/HostCore` runtime orchestration and feature services
- `host/WpfHost` operator-facing controls and diagnostics UX
- `tools/*` automation scripts for validation, release visibility, and version-contract checks

This round intentionally follows WPF-first host policy and keeps WinUI as diagnostics/track-status visibility.

## Implemented Changes

### 1) HostCore feature foundation (`R01/R03/R08/R09/R10/R11/R12/R13/R17/R19/R20`)

Added:

- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.MvpFeatures.cs`

Updated:

- `host/HostCore/HostController.cs` (converted to `partial` and integrated feature hooks)

Key additions:

- Preflight checks and summary contract (`RunPreflight`, `PreflightSummary`, `PreflightCheckResult`) (`R01`)
- User-facing error mapping (`UserFacingError`, category+remediation) integrated into operation logs (`R03`)
- Session persistence model/store (avatar path, output config, sidecar settings, selected profile) (`R08`)
- Diagnostics bundle export (snapshot/logs/preflight/guides/telemetry zipped artifact) (`R09`)
- Rolling runtime metrics capture + CSV export (`R10`)
- In-product compatibility/fallback text providers (`GetCompatibilityText`, `GetQuickstartText`) (`R11`, `R20`)
- Sidecar runtime configuration API (mode/path/timeout/strict) with env-var application (`R12`)
- Async avatar load API with timeout/cancel contract (`LoadAvatarAsync`, `CancelLoadAvatar`) (`R13`)
- Release track status surface (`GetReleaseTrackStatus`) from diagnostics manifest (`R17`)
- Telemetry opt-in/redaction/export controls (`SetTelemetryPolicy`, `ExportTelemetry`) (`R19`)

Additional behavior hardening:

- Operation-level lifecycle guard in `ExecuteOperation` (`ValidateOperationAllowed`) to enforce state machine order (`R07`)
- Auto quality guardrail via sustained frame-time pressure detection and profile downgrade (`RecordFrameMetricAndGuardrails`) (`R05`)
- `.miq` input extension validation path added in input validator (`R02` compatibility path completion)

### 2) WPF host UX and operator controls (`R01/R02/R04/R05/R09/R10/R11/R12/R13/R17/R19/R20`)

Updated:

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

UI additions:

- New `Platform Ops` section:
  - `Run Preflight`
  - `Export Diagnostics`
  - `Export Metrics`
  - profile buttons (`Quality`, `Performance`, `Stability`)
  - sidecar configuration fields (mode/path/timeout/strict)
  - telemetry controls (opt-in/redact/export)
- New `Guides` diagnostics tab (quickstart + troubleshooting + compatibility/fallback guidance)
- Status strip extension with host-track indicator (`WPF/WinUI` track status)

Behavior updates:

- Avatar file dialog filter expanded to include `.miq` (`R02`)
- `Load` action moved to async+timeout path (`LoadAvatarAsync`) with immediate import-route guidance (`R13`)
- New event handlers wired for preflight, diagnostics export, metrics export, profile apply, sidecar apply, telemetry apply/export
- Session defaults are loaded into UI controls at startup from persisted model (`R08`)

### 3) Tooling automation (`R14/R16/R18`)

Added:

- `tools/avatar_batch_validate.ps1` (`R14`)
  - batch scan/validation summary for supported avatar containers
  - JSON + TXT summary artifacts
- `tools/release_gate_dashboard.ps1` (`R16`)
  - aggregates existing gate/publish report lines into a dashboard summary
  - JSON + TXT outputs
- `tools/version_contract_check.ps1` (`R18`)
  - verifies key contract fields (HostCore target framework + Unity package version metadata)
  - emits summary report and non-zero exit on contract failure

## Verification Summary

Build verification:

```powershell
dotnet build NativeAnimiq\host\HostCore\HostCore.csproj -c Release
dotnet build NativeAnimiq\host\WpfHost\WpfHost.csproj -c Release --no-restore
```

Result:

- `HostCore`: PASS (`0 warnings`, `0 errors`)
- `WpfHost`: PASS (`0 warnings`, `0 errors`)

Tool script verification:

```powershell
powershell -ExecutionPolicy Bypass -File NativeAnimiq\tools\avatar_batch_validate.ps1 -InputDir sample
powershell -ExecutionPolicy Bypass -File NativeAnimiq\tools\release_gate_dashboard.ps1 -ReportDir NativeAnimiq\build\reports
powershell -ExecutionPolicy Bypass -File D:\dbslxlvseefacedkfb\NativeAnimiq\tools\version_contract_check.ps1 -HostCoreProject D:\dbslxlvseefacedkfb\NativeAnimiq\host\HostCore\HostCore.csproj -UnityPackageJson D:\dbslxlvseefacedkfb\NativeAnimiq\unity\Packages\com.animiq.miq\package.json -OutputTxt D:\dbslxlvseefacedkfb\NativeAnimiq\build\reports\version_contract_check.txt
```

Result:

- `avatar_batch_validate.ps1`: PASS
- `release_gate_dashboard.ps1`: PASS
- `version_contract_check.ps1`: PASS

## Requirement Coverage Snapshot

- `R01`: preflight wizard baseline + surfaced checks (MVP)
- `R02`: unified import guidance + supported extension path updates
- `R03`: user-facing error taxonomy and remediation hints
- `R04`: render profile presets (quality/performance/stability)
- `R05`: sustained frame-time auto quality guard
- `R06`: output reconcile/self-heal path retained and integrated
- `R07`: strict operation-order guards in HostController
- `R08`: crash-safe session state persistence/restore
- `R09`: one-click diagnostics bundle export
- `R10`: runtime rolling metrics + export
- `R11`: compatibility matrix/fallback policy surfaced in-app
- `R12`: sidecar parser control surface and persistence
- `R13`: async load + timeout/cancel API and UI integration
- `R14`: batch validation CLI script
- `R15`: existing Unity MIQ validation pipeline remains the SDK validator baseline in this round
- `R16`: release gate dashboard script
- `R17`: user-visible track status (WPF ready / WinUI blocked or unknown)
- `R18`: version contract checker
- `R19`: telemetry privacy controls (opt-in/redact/export)
- `R20`: quickstart + troubleshooting decision text embedded in host UI

## Known Limitations

- WinUI parity for newly added WPF UI controls is not completed in this round (WPF-first policy).
- `R15` advanced validator enhancements were not expanded beyond existing Unity validation contract.
- Async load cancel is cooperative at host orchestration layer; native deep cancellation is not introduced.

## Next Steps

1. Port `Platform Ops` UI and guide/track surfaces to `WinUiHost` for parity.
2. Expand Unity-side validator depth (`R15`) with explicit section-level compatibility assertions.
3. Add CI invocation for new tooling scripts (`avatar_batch_validate`, `release_gate_dashboard`, `version_contract_check`).
