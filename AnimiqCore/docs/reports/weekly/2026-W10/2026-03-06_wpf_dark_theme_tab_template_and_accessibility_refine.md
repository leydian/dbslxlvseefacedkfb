# WPF dark theme tab template and accessibility refine (2026-03-06)

## Summary

This update implements the planned dark-theme refinement for the WPF host UI with a focus on:

- removing remaining light-theme islands in diagnostics tabs
- improving control state readability (focus/disabled)
- reducing theme drift risk by converting runtime theme assignment to palette maps

Scope is limited to WPF UI resources and theme application logic.

## Why this change

In the previous dark-theme pass, semantic tokens were expanded and hardcoded colors were removed from many sections.
However, the diagnostics area on the right side could still appear visually bright due to default control template behavior.
In addition, keyboard focus and disabled states relied on weak visual signals.

This pass fixes those structural gaps.

## Detailed changes

### 1) New semantic tokens in `App.xaml`

Added new brush tokens to support panel hierarchy, focus visibility, disabled state clarity, and diagnostics text areas:

- `Color.PanelBase`
- `Color.PanelElevated`
- `Color.PanelInset`
- `Color.FocusRing`
- `Color.DisabledBg`
- `Color.DisabledText`
- `Color.LogAreaBg`
- `Color.LogAreaBorder`

These are used across control templates and state styles so dark/light variants remain consistent.

### 2) Interaction states improved in base styles

Updated default `Button` style:

- keyboard focus now uses explicit 2px focus ring (`Color.FocusRing`)
- disabled state now uses dedicated disabled colors instead of opacity-only fade

Updated default `TextBox` style:

- keyboard focus ring added for input clarity and keyboard accessibility

Added `GhostButtonStyle` for low-emphasis actions with deterministic hover/pressed surfaces.

### 3) Diagnostics area no longer falls back to system light surface

Replaced default look with explicit templates:

- custom `TabControl` template
- custom `TabItem` template

The selected content host now uses theme resources (`Color.PanelElevated`) instead of framework defaults.
This removes the white-pane artifact in dark mode.

### 4) Read-only diagnostics text areas unified

Added `LogReadOnlyTextBoxStyle` and applied it to:

- Runtime diagnostics textbox
- Avatar diagnostics textbox
- Logs textbox
- Guides textbox

This ensures read-only diagnostics surfaces and borders always follow theme tokens.

### 5) Theme runtime application refactor (`MainWindow.xaml.cs`)

`ApplyThemeResources()` was refactored:

- from repetitive per-key assignments split by conditional blocks
- to two explicit palette dictionaries (`darkPalette`, `lightPalette`) iterated uniformly

Benefits:

- lower chance of missing token updates between themes
- easier visual tuning per theme revision
- straightforward extension when new `Color.*` keys are introduced

Also tuned dark text contrast for muted/subtle text (`Color.TextMuted`, `Color.TextSubtle`) to improve readability.

## Files changed

- `host/WpfHost/App.xaml`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`

## Validation

- Build:
  - `dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -c Debug`
  - Result: PASS (`0 warnings`, `0 errors`)

- Theme coverage checks:
  - diagnostics tab content now uses explicit themed template host
  - all four diagnostics textboxes now use themed read-only style

## Compatibility

- No native/runtime API contract changes
- No persistence schema changes
- No WinUI host changes in this pass
