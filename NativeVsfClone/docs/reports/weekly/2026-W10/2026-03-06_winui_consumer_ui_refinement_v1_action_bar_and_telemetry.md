# WinUI consumer UI refinement v1: onboarding action bar parity and UX telemetry wiring (2026-03-06)

## Scope

This report documents the WinUI parity pass for the consumer-facing onboarding UX already established in WPF.

In scope:
- WinUI onboarding action bar surface
- WinUI onboarding step/progress and recovery hint presentation
- WinUI event wiring for onboarding UX telemetry parity
- WinUI CTA hierarchy and enable/disable policy alignment with HostUiPolicy

Out of scope:
- HostCore API contract expansion (already completed in prior WPF-centered pass)
- WinUI full visual token system extraction
- native runtime behavior and rendering pipeline changes

## Implemented Changes

### 1) Added a top-level onboarding action surface in WinUI

Updated `host/WinUiHost/MainWindow.xaml`:
- inserted a top onboarding action bar section (new first rows in left control column)
- added key controls:
  - `QuickNextActionText`
  - `ActionabilityBadgeText` (`READY`/`BLOCKED`)
  - `PrimaryActionDescriptionText`
  - `NextActionSummaryText`
  - `BlockReasonShortText`
  - `PrimaryActionButton`
  - quick secondary buttons (`QuickInitializeButton`, `QuickLoadAvatarButton`, `QuickStartBroadcastButton`)
- added step progress strip:
  - `OnboardingStep1Text`, `OnboardingStep2Text`, `OnboardingStep3Text`
- added compact status/recovery panel:
  - `QuickStatusText`
  - `OnboardingRecoveryText`
  - `OpenDiagnosticsFromHintButton`, `DismissRecoveryHintButton`
- shifted existing section row indices down to keep legacy content intact.

Behavioral impact:
- first glance now exposes a single “what next” path before detailed controls.

### 2) Wired WinUI to HostCore onboarding policy contract

Updated `host/WinUiHost/MainWindow.xaml.cs`:
- `UpdateUiState()` now reads `HostUiPolicy.BuildOnboardingState(...)`
- binds onboarding fields directly to new UI controls:
  - step title/instruction/summary/short block reason/actionability
- updates badge style semantics:
  - blocked -> warning-toned `BLOCKED`
  - actionable -> neutral-toned `READY`
- computes and displays 3-step completion state.

Behavioral impact:
- WinUI now uses the same decision source as WPF for onboarding guidance.

### 3) Added WinUI primary/secondary CTA flow and recovery actions

Updated `host/WinUiHost/MainWindow.xaml.cs`:
- new handlers:
  - `PrimaryAction_Click`
  - `QuickInitialize_Click`
  - `QuickLoadAvatar_Click`
  - `QuickStartBroadcast_Click`
  - `OpenDiagnosticsFromHint_Click`
  - `DismissRecoveryHint_Click`
- `PrimaryAction_Click` dispatches action by `HostPrimaryActionKind`.
- quick start output path follows fallback behavior:
  - try Spout first, then OSC if needed/available.
- introduced local `_recoveryHint` state to keep immediate user-action context visible.

Behavioral impact:
- WinUI now mirrors WPF’s single-primary-action onboarding behavior and recovery affordance.

### 4) Added WinUI onboarding UX telemetry parity

Updated `host/WinUiHost/MainWindow.xaml.cs`:
- emits `primary_cta_clicked` on primary action press
- emits `onboarding_step_viewed` on step transitions
- emits `recovery_action_clicked` on diagnostics jump from recovery hint
- telemetry payload comes from existing HostCore `TrackOnboardingUiEvent(...)` contract.

Behavioral impact:
- WinUI and WPF onboarding funnel analytics are now schema-aligned.

## Verification Summary

Executed:

```powershell
dotnet build host\WinUiHost\WinUiHost.csproj -c Release --no-restore /p:BuildProjectReferences=false
```

Outcome:
- FAIL at WinUI XAML compiler stage (`XamlCompiler.exe` exit code 1) in this environment, without line-level diagnostic details.

Additional observation:
- workspace also contains unrelated existing HostCore compile breakage when project references are enabled (`DiagnosticsModel` constructor mismatch), not introduced by this WinUI parity change set.

## Known Risks or Limitations

- WinUI markup compiler in current environment may fail without actionable line diagnostics; local toolchain health check is still required.
- WinUI currently uses inline colors for this pass; full tokenization parity with WPF resource system is not yet completed.
- Recovery hint behavior is intentionally lightweight and local-state-based for this iteration.

## Next Steps

1. Stabilize WinUI XAML toolchain diagnostics (ensure output json/log surfaces line-level failures).
2. Move WinUI onboarding visual values to a centralized token/resource layer.
3. Add automated parity check ensuring WPF/WinUI onboarding event field consistency.
