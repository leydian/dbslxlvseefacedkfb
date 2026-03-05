# WPF consumer UI uplift: onboarding flow, visual system, and 3-minute success telemetry (2026-03-06)

## Scope

This report covers the WPF host transition toward a consumer-first UI baseline focused on first-run completion speed.

In scope:
- onboarding-oriented UI state model and policy
- beginner-mode UX simplification and primary action flow
- app-wide WPF visual style tokens for a friendlier media-app tone
- telemetry milestones to measure `first-run output start within 3 minutes`

Out of scope:
- WinUI parity implementation
- nativecore rendering/runtime behavior changes unrelated to onboarding milestones

## Implemented Changes

### 1) Host UI contract and onboarding policy

- Added onboarding domain types to the shared UI state contract:
  - `HostOnboardingStep`
  - `HostPrimaryActionKind`
  - `HostOnboardingState`
- Added `HostUiPolicy.BuildOnboardingState(...)` to compute:
  - current onboarding step
  - single primary CTA intent
  - user-facing instruction
  - block reason and recovery action

Behavioral decision order:
1. busy -> blocked state
2. uninitialized -> initialize CTA
3. invalid avatar path -> block + recovery guidance
4. no loaded avatar -> load CTA
5. no output active -> start-output CTA
6. otherwise ready

Updated instruction copy to action-oriented phrasing for consumer users.

### 2) WPF onboarding UX and beginner-mode simplification

- Replaced the old quick-start text block with a structured onboarding card:
  - step title
  - instruction text
  - single primary action button
  - 3-step progress indicators
  - recovery panel
- Implemented `PrimaryAction_Click` routing to enforce the onboarding action sequence.
- Added start-output fallback behavior in primary CTA path:
  - attempt Spout first
  - if still inactive and OSC is available, attempt OSC
- Beginner mode visibility tightened to reduce cognitive load:
  - hides Session/Render/Outputs groups unless advanced mode is selected
- Added step-state visual labeling (`completed` vs `next step`) with explicit color signaling.

### 3) App-wide WPF visual system

Added global resource tokens/styles in `WpfHost/App.xaml`:
- surface, card, border, primary, text color brushes
- implicit styles for:
  - `Window`
  - `GroupBox`
  - `Button` (hover/disabled treatment)
  - `TextBox`
  - `ComboBox`
  - `CheckBox`
  - `TextBlock`

Result:
- consistent card-based visual hierarchy
- clearer primary-action emphasis
- more product-like appearance vs operator-tool baseline

### 4) Telemetry milestones for onboarding KPI

Added onboarding timestamps and success flag in HostCore telemetry:
- `session_started_at`
- `initialized_at`
- `avatar_loaded_at`
- `output_started_at`
- `within_3min_success`

Added milestone event stream:
- event name: `onboarding_milestone`
- milestones emitted:
  - `session_started`
  - `initialized`
  - `avatar_loaded`
  - `output_started:spout` / `output_started:osc`

Milestone hooks are triggered on successful operation completion paths.

## Verification Summary

Executed command:

```powershell
dotnet build host\WpfHost\WpfHost.csproj -c Release
```

Outcome:
- PASS (0 warnings, 0 errors)

Notes:
- solution-wide build can still fail on WinUI XAML toolchain in this environment; this report validates WPF-first change set.

## Known Risks or Limitations

- Primary CTA start-output path can trigger OSC attempt after Spout attempt; if both are valid this is intentional for first-run success, but product policy may later restrict to one output by default.
- Visual style tokens are currently WPF host-local and not yet mirrored to WinUI.
- 3-minute KPI is measured via telemetry events but does not yet have a dedicated aggregated dashboard artifact.

## Next Steps

1. Add telemetry aggregation script/report for onboarding milestone funnel and 3-minute success rate.
2. Add a lightweight runtime smoke that validates primary CTA sequence end-to-end in WPF.
3. Plan WinUI parity pass reusing the same onboarding policy contract.
