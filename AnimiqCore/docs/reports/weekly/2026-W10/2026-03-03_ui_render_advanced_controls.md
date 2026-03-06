# UI Render Advanced Controls + Preset Persistence (2026-03-03)

## Summary

Added advanced render composition controls and local preset persistence to both WPF and WinUI hosts.

This update extends the existing render panel from basic framing/background/overlay controls to full operator-level composition workflow with reusable presets.

## Scope

- `host/HostCore`
- `host/WpfHost`
- `host/WinUiHost`

## Implemented Changes

1. HostCore preset contract and storage
- Added `IRenderPresetStore` interface.
- Added new preset model/storage file:
  - `RenderPresetModel`
  - `RenderPresetStoreModel`
  - `RenderPresetStore`
- Persistence target:
  - user-local JSON file under LocalAppData (`AnimiqHost/render_presets.json`).
- Added load-time normalization:
  - duplicate-name collapse (case-insensitive)
  - value clamping for numeric render fields
  - default preset fallback when file is missing/invalid
  - best-effort `.bak` backup when parsing fails

2. HostController render/preset orchestration
- Added preset API surface:
  - `CreatePreset(name)`
  - `SaveOrUpdateRenderPreset(name)`
  - `ApplyRenderPreset(name)`
  - `DeleteRenderPreset(name)`
  - `ResetRenderDefaults()`
- Added preset exposure for UI sync:
  - `RenderPresets`
  - `SelectedRenderPresetName`
- Updated `SetBroadcastMode(...)` behavior:
  - preserves user composition controls (`camera/framing/headroom/yaw/fov/mirror/overlay`) while switching broadcast baseline.
- Added centralized render-state normalization and clamp path used by both direct UI apply and preset apply.

3. WPF host UI upgrade
- Render panel additions:
  - `Camera Mode` (`Auto Fit Full`, `Auto Fit Bust`, `Manual`)
  - `Headroom` slider
  - `Yaw` slider
  - `FOV` slider
  - `Mirror Mode` toggle
  - preset controls (`Save`, `Apply`, `Delete`, `Reset To Default`)
- Added render apply debounce timer (`~100ms`) for slider interactions.
- Added preset list/name sync behavior with HostCore preset state.
- Expanded runtime diagnostics line with advanced fields (`headroom`, `yaw`, `fov`, `mirror`).

4. WinUI host UI upgrade
- Added the same advanced controls and preset workflow as WPF for parity.
- Added matching debounce behavior and diagnostics field expansion.
- Added same enable/disable gating by session state.

## Behavior Notes

- Render controls and preset actions are disabled until session initialization.
- Preset delete is blocked when only one preset remains.
- Preset names are normalized by trimming.
- Numeric ranges enforced through slider bounds and HostCore clamping:
  - Framing: `0.35 - 0.95`
  - Headroom: `0.00 - 0.50`
  - Yaw: `-45 - 45`
  - FOV: `20 - 70`

## Verification

- `dotnet build host/HostCore/HostCore.csproj -c Release`: pass
- WPF/WinUI project builds are environment-blocked by NuGet connectivity in current sandbox (`NU1301`, `api.nuget.org:443` unreachable), so full GUI-host compile validation remains to be confirmed in network-enabled CI/local environment.

## Files

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/RenderPresetStore.cs` (new)
- `host/HostCore/HostController.cs`
- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`
