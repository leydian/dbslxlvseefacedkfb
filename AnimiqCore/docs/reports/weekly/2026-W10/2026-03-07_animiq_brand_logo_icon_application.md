# 2026-03-07 Animiq Brand Logo and Icon Application

## Scope
- Applied user-provided branding assets to both host applications:
- `WpfHost` executable/window icon
- `WinUiHost` executable/window icon
- Left-rail brand UI blocks in WPF and WinUI
- Included source assets in-repo for traceability and reproducibility.

## Implemented Changes
- Asset ingestion and conversion
- Source assets added under `host/Branding/source`:
- `animiq_logo.pdf`
- `animiq_miq_icon.svg`
- `animiq_miq_icon.ai`
- Converted outputs committed as app-ready binaries:
- `WpfHost/Assets/app.ico`
- `WpfHost/Assets/brand-icon.png`
- `WpfHost/Assets/brand-logo.png`
- `WinUiHost/Assets/app.ico`
- `WinUiHost/Assets/brand-icon.png`
- `WinUiHost/Assets/brand-logo.png`

- WPF host wiring
- `host/WpfHost/WpfHost.csproj`
- Added `<ApplicationIcon>Assets\app.ico</ApplicationIcon>`
- Added Resource includes for `app.ico`, `brand-icon.png`, `brand-logo.png`
- `host/WpfHost/MainWindow.xaml`
- Added `Icon="Assets/app.ico"` on root `Window`
- Replaced legacy text badge (`V`) with image-based brand block:
- `pack://application:,,,/Assets/brand-icon.png`
- `pack://application:,,,/Assets/brand-logo.png`

- WinUI host wiring
- `host/WinUiHost/WinUiHost.csproj`
- Added `<ApplicationIcon>Assets\app.ico</ApplicationIcon>`
- Added copy-to-output entries for brand assets
- `host/WinUiHost/MainWindow.xaml`
- Added top-left rail brand card using:
- `ms-appx:///Assets/brand-icon.png`
- `ms-appx:///Assets/brand-logo.png`
- `host/WinUiHost/MainWindow.xaml.cs`
- Added `TrySetWindowIcon()` and call in constructor
- Applied runtime window icon via `AppWindow.SetIcon(...)`

## Verification Summary
- Asset existence checks
- Confirmed source files from user path were present and readable.
- Confirmed generated/committed app assets existed in both host `Assets` folders.
- Build checks
- `dotnet build AnimiqCore\host\HostApps.sln -c Release -v minimal`
- Restore succeeded after elevated network run.
- Build failed on pre-existing unrelated code issue:
- `CS0535` in `host/HostCore/AvatarSessionService.cs`
- `AvatarSessionService` does not implement `IAvatarSessionService.LoadAvatar(string)`

## Known Risks or Limitations
- Full solution compile is currently blocked by existing HostCore interface mismatch unrelated to this branding change.
- PDF/SVG to app assets were converted during integration; the conversion toolchain itself is not yet scripted as a checked-in pipeline.

## Next Steps
- Fix HostCore interface mismatch (`CS0535`) and rerun full host build verification.
- Optionally add a small reproducible conversion script under `host/Branding` for future branding updates.
