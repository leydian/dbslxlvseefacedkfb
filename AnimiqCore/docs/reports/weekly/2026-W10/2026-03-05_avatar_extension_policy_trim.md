# Avatar Extension Policy Trim (2026-03-05)

## Summary

Repository avatar input policy was narrowed from five extensions to three:

- kept: `.vrm`, `.vsfavatar`, `.miq`
- removed from runtime support: `.vxavatar`, `.vxa2`

This update aligns loader registration, host-side validation/UI filters, and top-level documentation with the same support contract.

## Implementation Changes

### 1) Native runtime loader path trimmed

- `src/avatar/avatar_loader_facade.cpp`
  - removed `VxAvatarLoader` and `Vxa2Loader` includes and registration
  - active loaders are now:
    - `VrmLoader`
    - `MiqLoader`
    - `VsfAvatarLoader`
- `CMakeLists.txt`
  - removed `src/avatar/vxavatar_loader.cpp`
  - removed `src/avatar/vxa2_loader.cpp`
- removed legacy loader files:
  - `src/avatar/vxavatar_loader.h`
  - `src/avatar/vxavatar_loader.cpp`
  - `src/avatar/vxa2_loader.h`
  - `src/avatar/vxa2_loader.cpp`

### 2) Host input policy updated (WPF/WinUI shared core)

- `host/HostCore/HostController.cs`
  - `TryValidateAvatarPath` now accepts only:
    - `.vrm`
    - `.vsfavatar`
    - `.miq`
- `host/HostCore/HostController.MvpFeatures.cs`
  - removed `.vxavatar` and `.vxa2` import plan branches
  - unsupported extension guidance now advertises:
    - `.vrm, .vsfavatar, .miq`

### 3) Host UI picker filters updated

- `host/WpfHost/MainWindow.xaml.cs`
  - open dialog filter changed to:
    - `*.vrm;*.vsfavatar;*.miq`
- `host/WinUiHost/MainWindow.xaml.cs`
  - file picker types reduced to:
    - `.vrm`
    - `.vsfavatar`
    - `.miq`

### 4) User-facing host text updated

- `host/HostCore/PlatformFeatures.cs`
  - quickstart import step now states:
    - `(.vrm/.miq/.vsfavatar)`
  - compatibility matrix entries for `.vxavatar` and `.vxa2` removed

### 5) README support contract aligned

- `README.md`
  - top feature list now reflects `.vrm/.miq/.vsfavatar`
  - loader facade extension list updated to the same three formats
  - current behavior status section now lists `.miq` behavior instead of `.vxavatar`
  - top validation snapshot line for `vxavatar` gate removed

## Verification

Executed after code changes:

1. Native core build
   - command: `cmake --build NativeAnimiq/build --config Release --target animiq_core`
   - result: PASS
2. HostCore build
   - command: `dotnet build NativeAnimiq/host/HostCore/HostCore.csproj -c Release`
   - result: PASS
3. WPF host build
   - command: `dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -c Release`
   - result: PASS
4. WinUI host build
   - command: `dotnet build NativeAnimiq/host/WinUiHost/WinUiHost.csproj -c Release`
   - result: FAIL
   - failure class: existing XAML compile/toolchain issue (`MSB3073` in XamlCompiler path), unrelated to extension-filter edits

## Notes

- `AvatarLoaderFacade` still retains signature fallback behavior, but fallback scope now effectively covers only the three remaining registered loaders.
- Legacy `AvatarSourceType` enum values (`VxAvatar`, `Vxa2`) remain in shared API types for backward enum compatibility; they are no longer reachable via current loader registration.
