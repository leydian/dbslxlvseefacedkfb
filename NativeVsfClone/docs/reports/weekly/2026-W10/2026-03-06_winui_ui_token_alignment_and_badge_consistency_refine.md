# WinUI UI token alignment and onboarding badge consistency refinement (2026-03-06)

## Summary

This pass finalizes the next-stage WinUI visual consistency work after the earlier WPF+WinUI modernization baseline.

Goal:
- reduce style drift in WinUI onboarding/status surfaces
- eliminate remaining hardcoded actionability badge colors in code-behind
- move key onboarding/step/card semantics to explicit token resources

The runtime behavior and event flow contracts remain unchanged.

## Scope

In scope:
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`

Out of scope:
- HostCore contract changes
- native runtime/render behavior
- release packaging flow

## Implemented Changes

### 1) Expanded WinUI semantic tokens for onboarding/step/status surfaces

Updated `host/WinUiHost/MainWindow.xaml` resources:
- added onboarding palette tokens:
  - `Brush.OnboardingBg`, `Brush.OnboardingBorder`, `Brush.OnboardingTitle`, `Brush.OnboardingBody`
- added badge state tokens:
  - `Brush.BadgeReadyBg`, `Brush.BadgeReadyBorder`, `Brush.BadgeReadyText`
  - `Brush.BadgeBlockedBg`, `Brush.BadgeBlockedBorder`, `Brush.BadgeBlockedText`
- added step-strip tokens:
  - `Brush.StepStripBg`, `Brush.StepStripBorder`, `Brush.StepStripText`
- added validation/error token:
  - `Brush.Error`

Behavioral impact:
- no logic change; visual semantics become centralized and easier to retune.

Detailed footprint:
- token declarations expanded in `MainWindow.xaml` near window resource block
- onboarding/status related token families are now grouped semantically rather than left as one-off literals
- token naming follows existing `Brush.*` pattern to remain compatible with current resource lookup style

### 2) Onboarding action bar styling moved to semantic token usage

Updated onboarding surfaces in `MainWindow.xaml`:
- action bar container switched to onboarding token colors
- title/body text switched to onboarding token colors
- step strip border/background/step-text switched to step-strip token colors
- primary/secondary cards normalized to `Brush.CardBorder` for structural consistency
- avatar path validation text now uses `Brush.Error`

Behavioral impact:
- improved visual coherence across onboarding states and section cards.

Detailed footprint:
- onboarding panel: border/background/title/body now resource-driven
- step strip: background/border/text color moved to resource keys
- status and section card borders normalized to `Brush.CardBorder` for cross-panel consistency
- validation text path updated from literal red to `Brush.Error`

### 3) Actionability badge state color hardcoding removed from code-behind

Updated `host/WinUiHost/MainWindow.xaml`:
- badge border assigned x:Name: `ActionabilityBadgeBorder`

Updated `host/WinUiHost/MainWindow.xaml.cs`:
- replaced direct `SolidColorBrush(ColorHelper...)` assignments with token lookup via `ResolveBrush(...)`
- READY/BLOCKED now consistently set:
  - badge text color
  - badge background
  - badge border
- added helper:
  - `ResolveBrush(string key)`

Behavioral impact:
- badge state style now follows resource contract and stays in sync with theme/token changes.

Detailed footprint:
- `ActionabilityBadgeBorder` named element added in XAML for code-behind state styling
- `ResolveBrush(string key)` centralizes resource resolution and null-safe fallback behavior
- runtime state transitions now update all three badge visuals (text/bg/border) through token keys only

## Verification

Executed:
1. `dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore`
- PASS (`0 warnings`, `0 errors`)

2. `dotnet build NativeVsfClone\host\WinUiHost\WinUiHost.csproj -c Release --no-restore /p:BuildProjectReferences=false`
- FAIL in this environment at WinUI markup compiler step (`XamlCompiler.exe` exit code `1`), same environment-dependent diagnostic limitation as before.

Manual review checks completed:
- confirmed no remaining `ColorHelper` hardcoded badge color writes in `MainWindow.xaml.cs`
- confirmed onboarding badge state branch (`READY` / `BLOCKED`) now maps to resource keys for text/background/border
- confirmed resource keys referenced in code exist in XAML resource dictionary

## Risks / Limitations

- WinUI compile verification is still constrained by current environment-level XAML compiler diagnostics.
- This pass improves style consistency but does not yet introduce a shared cross-host token source between WPF `App.xaml` and WinUI resources.

Acceptance checklist status:
- `Actionability badge literal colors removed`: DONE
- `Onboarding/step color semantics tokenized`: DONE
- `Primary status/card border consistency pass`: DONE
- `WinUI compile pass with line-level diagnostics`: BLOCKED (environment)

## Next Steps

1. Add a deterministic WinUI XAML diagnostic capture path (line-level failure details) to unblock stronger verification.
2. Consolidate repeated token intent between WPF and WinUI into a documented cross-host token mapping contract.
3. Run focused UX smoke for onboarding state transitions (`READY`/`BLOCKED`) to validate visual-state parity under runtime changes.
