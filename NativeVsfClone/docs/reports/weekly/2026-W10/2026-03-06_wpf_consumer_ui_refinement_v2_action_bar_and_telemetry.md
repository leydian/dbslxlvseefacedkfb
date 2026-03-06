# WPF consumer UI refinement v2: action bar, consumer nav scope, and onboarding UX telemetry (2026-03-06)

## Scope

This report summarizes the March 6, 2026 refinement pass that implemented the approved consumer-facing WPF UI uplift plan.

In scope:
- WPF-first UX refinement for beginner-heavy workflows
- onboarding contract extension in HostCore used by WPF
- onboarding UI interaction telemetry events
- consumer-toned visual token refresh and action hierarchy styles

Out of scope:
- WinUI parity implementation
- native runtime/rendering behavior changes
- release pipeline or packaging changes

## Implemented Changes

### 1) HostCore onboarding contract became actionability-aware

Updated `host/HostCore/HostUiState.cs`:
- added `HostActionability` enum (`Immediate`, `Blocked`)
- extended `HostOnboardingState` with:
  - `NextActionSummary`
  - `BlockReasonShort`
  - `Actionability`

Updated `host/HostCore/HostUiPolicy.cs`:
- all onboarding states now emit short-form CTA context and blockability state
- blocked states now carry concise actionable messaging for top-level UI display

Behavioral impact:
- WPF can render a single compact “what to do now” surface without re-deriving policy in view code

### 2) WPF Getting Started became a fixed top action bar flow

Updated `host/WpfHost/MainWindow.xaml`:
- replaced previous quick-start card composition with a structured onboarding action bar:
  - `ActionabilityBadgeText` (`READY` / `BLOCKED`)
  - `QuickNextActionText`
  - `PrimaryActionDescriptionText`
  - `NextActionSummaryText`
  - `BlockReasonShortText`
- kept one dominant primary CTA (`PrimaryActionButton`)
- retained fast step buttons as secondary controls (`세션`, `아바타`, `출력`)
- refreshed step cards and recovery area button hierarchy for clearer affordance

Updated `host/WpfHost/MainWindow.xaml.cs`:
- action bar bindings now consume new onboarding fields
- step transitions emit onboarding view telemetry only on step changes
- badge/short-block-reason visual updates are driven by policy result

Behavioral impact:
- beginner users get one obvious next action with less scanning cost
- blocked states become immediately legible at top of flow

### 3) Beginner-mode nav scope expanded for consumer task completion

Updated `host/WpfHost/MainWindow.xaml.cs`:
- beginner mode now allows direct navigation to:
  - `Getting Started`
  - `Session / Avatar`
  - `Outputs`
- advanced-only sections remain:
  - `Render`
  - `Tracking`
  - `Platform Ops`
- non-consumer sections are still auto-clamped back to `Getting Started` in beginner mode

Behavioral impact:
- preserves low complexity while avoiding dead-end navigation in common setup flows

### 4) Consumer visual token refresh and button hierarchy styles

Updated `host/WpfHost/App.xaml`:
- retuned light-theme token palette toward neutral, broadly familiar consumer tone
- added onboarding-specific visual tokens:
  - `Color.OnboardingBar*`
  - `Color.BadgeNeutral*`
- added reusable action hierarchy styles:
  - `SecondaryButtonStyle`
  - `TertiaryButtonStyle`

Updated `host/WpfHost/MainWindow.xaml.cs`:
- synchronized runtime light/dark theme brush updates with new token keys

Behavioral impact:
- stronger visual clarity between primary/secondary/tertiary actions
- more mass-market, less operator-heavy tone without structural rewrite

### 5) Onboarding UX telemetry events added

Updated `host/HostCore/HostController.MvpFeatures.cs`:
- added `TrackOnboardingUiEvent(...)`

Updated `host/WpfHost/MainWindow.xaml.cs` event points:
- `onboarding_step_viewed` (on step transition)
- `primary_cta_clicked`
- `recovery_action_clicked`

Telemetry payload includes:
- `step`, `primary_action`, `actionability`, `reason`
- onboarding milestone timestamps and `within_3min_success`

Behavioral impact:
- enables KPI/funnel analysis for UI comprehension and friction points

## Verification Summary

Executed:

```powershell
dotnet build host\WpfHost\WpfHost.csproj -c Release --no-restore
```

Result:
- PASS (0 warnings, 0 errors)

## Known Risks or Limitations

- WinUI host has not yet received parity for this onboarding action bar/telemetry surface.
- New telemetry events are emitted, but no dedicated aggregation dashboard update is included in this pass.
- `HostController.MvpFeatures.cs` had existing local modifications in the workspace before this pass; this change set adds onboarding UI telemetry method in that context.

## Next Steps

1. Add telemetry aggregation slices for new events (`step_viewed`, `primary_cta_clicked`, `recovery_action_clicked`).
2. Execute beginner-mode usability smoke with fresh-user scripts and record completion funnel delta.
3. Implement WinUI parity using the same onboarding policy contract and token intent.
