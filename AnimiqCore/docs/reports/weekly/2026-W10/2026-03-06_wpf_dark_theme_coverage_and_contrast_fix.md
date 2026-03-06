# WPF dark theme coverage and contrast fix (2026-03-06)

## Summary

This update resolves incomplete dark-theme application and low-contrast text issues in `WpfHost`.
The main goal was to eliminate hardcoded color islands so light/dark switching applies consistently across the full WPF operator UI.

Core outcomes:

- all hardcoded hex colors in `MainWindow.xaml` were replaced with `DynamicResource` theme keys
- onboarding/status/recovery/error/overlay areas now follow shared semantic tokens
- onboarding step state colors are now resource-driven (no direct brush creation)
- dark palette was expanded to improve readability and keep text/background contrast stable

## Scope

- Target:
  - `host/WpfHost/App.xaml`
  - `host/WpfHost/MainWindow.xaml`
  - `host/WpfHost/MainWindow.xaml.cs`
- Non-target:
  - `host/WinUiHost`
  - `host/HostCore`
  - native runtime/render contracts

## Detailed Changes

### 1) Theme token expansion (`App.xaml`)

Added semantic brush keys for UI regions that previously relied on hardcoded values:

- info/success/warning/error panel tokens
- validation error text token
- soft card background/border tokens
- splitter token
- render-only hint overlay tokens
- debug overlay tokens
- onboarding step state tokens (`complete` / `pending`)

Result: all major visual states now map to stable theme resources.

### 2) Hardcoded color removal (`MainWindow.xaml`)

Replaced all `#RRGGBB` / `#AARRGGBB` usage with dynamic theme references.

Updated areas include:

- mode/help muted explanatory text
- onboarding step cards and quick status card
- broadcast timing success panel
- action block reason warning panel
- beginner failure error panel
- recovery warning panel
- validation error text in avatar/OSC sections
- avatar preview card borders/background
- load/tracking/preflight status hints
- main grid splitter color
- render-only hint overlay colors
- debug overlay panel/text colors

Result: dark-theme toggling now covers previously missed sections.

### 3) Runtime theme application completion (`MainWindow.xaml.cs`)

`ApplyThemeResources()` now sets light/dark values for all newly introduced semantic keys.

Dark-side values were tuned for higher readability in low-luminance backgrounds while preserving the existing visual direction.

### 4) Code-behind brush creation cleanup (`MainWindow.xaml.cs`)

`SetOnboardingStepState(...)` was converted from direct brush creation:

- removed `new SolidColorBrush(Color.FromRgb(...))`
- now resolves `Color.StepComplete` / `Color.StepPending` via `FindResource(...)`

Result: state color also follows active theme at runtime.

## Verification

- Build:
  - `dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -c Debug`
  - PASS (`0 warnings`, `0 errors`)
- Static checks:
  - `MainWindow.xaml` hardcoded hex color scan: none remaining
  - all `MainWindow.xaml` `Color.*` references exist in `App.xaml`: verified

## Compatibility Notes

- No API/interface/schema changes.
- No behavior contract changes outside visual theming and readability.
- Existing theme toggle persistence (`dark` / `light`) remains unchanged.
