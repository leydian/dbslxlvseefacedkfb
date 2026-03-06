# WPF + WinUI UI Modernization 80pct Refresh (2026-03-06)

## Summary

This pass implements a broad consumer-facing UI modernization across both host shells while preserving existing runtime behavior and event wiring.

Applied direction:
- modern mass-market visual tone (light glass minimal)
- stronger action hierarchy (primary vs secondary)
- clearer workspace structure (left rail context + task canvas + render/status surface)

Primary objective was to increase perceived modernity and scanability without changing business logic or host-controller contracts.

## Scope

In scope:
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`

Out of scope:
- native runtime behavior
- HostCore API contract changes for this specific pass
- release pipeline or packaging flow

## Implemented Changes

### 1) WPF layout and visual hierarchy uplift

Updated `host/WpfHost/MainWindow.xaml`:
- added app-level gradient backdrop (`Brush.AppBackdrop`) to avoid flat background feel
- introduced reusable section presentation style (`SectionCardStyle`) and applied it to main task groups:
  - `ModeGroup`, `QuickActionsGroup`, `SessionGroup`, `AvatarGroup`, `RenderGroup`, `OutputsGroup`, `TrackingGroup`, `PlatformOpsGroup`
- upgraded left rail visual rhythm:
  - tightened spacing and button padding
  - added compact “권장 흐름” card to reinforce onboarding path
- wrapped control canvas in new card frame (`ControlPanelFrame`) for stronger page-level grouping
- adjusted control column width from `680` to `640` for improved proportion balance against preview pane

Behavioral compatibility:
- all existing `x:Name` controls and click/change handlers remain intact
- no flow logic changes in onboarding/session/avatar/output/tracking actions

### 2) WPF render-only compatibility update

Updated `host/WpfHost/MainWindow.xaml.cs`:
- `ApplyModeVisibility()` now toggles `ControlPanelFrame` visibility together with `ControlPanelScrollViewer`
- retained render-only mode behavior while reflecting new wrapper container
- synchronized normal mode width restoration to updated `640` value

Behavioral compatibility:
- render-only mode, diagnostics visibility rules, and status behavior stay functionally equivalent

### 3) WinUI tokenization and workspace restructuring

Updated `host/WinUiHost/MainWindow.xaml`:
- added local design token resources (`Brush.*`) for app background/card/text/status semantics
- added baseline control styles for `Border`, `TextBlock`, `Button`, and a soft hierarchy variant (`SoftButtonStyle`)
- converted main shell from two-column to three-column structure:
  - column 0: contextual left rail cards
  - column 1: task/control scroll canvas
  - column 2: render + diagnostics + status surface
- moved render/status region from `Grid.Column="1"` to `Grid.Column="2"` accordingly
- switched onboarding quick-action secondary buttons to `SoftButtonStyle`
- replaced direct warning/status color literals in key onboarding/status nodes with token references

Behavioral compatibility:
- existing control names and event handlers preserved
- onboarding and task actions remain routed through existing code-behind logic

## Verification

### Build checks executed

1. `dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore`
- Result: PASS
- Warnings: `0`
- Errors: `0`

2. `dotnet build NativeVsfClone\host\WinUiHost\WinUiHost.csproj -c Release --no-restore /p:BuildProjectReferences=false`
- Result: FAIL in current environment
- Failure point: WinUI markup compiler (`XamlCompiler.exe`) exits with code `1` without line-level diagnostics in this environment

### Additional notes

- WinUI failure matches previously observed environment-dependent compiler behavior and does not provide actionable line diagnostics here.
- WPF host build verifies new XAML structure and code-behind wiring compile cleanly.

## Risks / Limitations

- WinUI compile verification remains partially blocked by environment diagnostic limitation.
- WinUI styling is now tokenized at local window scope; full shared-token unification with WPF app-level resources is still a future consolidation step.

## Next Steps

1. Enable or script richer WinUI XAML compiler diagnostics (line-level output) and close the remaining build verification gap.
2. Run side-by-side usability smoke for first-run flow (`세션 -> 아바타 -> 출력`) on both hosts.
3. Perform a second pass for typography/copy consistency and spacing polish to maximize “80% transformation” perception.
