# Changelog

All notable implementation changes in this workspace are documented here.

## 2026-03-02 - NativeCore foundation + avatar pipeline extension

### Summary

Implemented the first end-to-end native runtime foundation for the VSeeFace-style standalone app effort.  
This update moves the project from a scaffold CLI into a reusable runtime DLL model with explicit API contracts and richer avatar compatibility diagnostics.

### Added

- `include/vsfclone/nativecore/api.h`
  - New exported C ABI contract for host applications.
  - Stable primitive structs for init/load/render/tracking/broadcast flows.
  - Error/result codes designed for cross-language interop.

- `src/nativecore/native_core.cpp`
  - Runtime state manager with guarded global state (`std::mutex`).
  - Avatar handle lifecycle (`load -> query -> unload`).
  - Last-error propagation via `nc_get_last_error`.
  - Tracking and render entrypoints stabilized as callable placeholders.
  - Spout/OSC integration points wired to existing stub backends.

- `src/avatar/vxavatar_loader.h`
- `src/avatar/vxavatar_loader.cpp`
  - New `.vxavatar` loader route.
  - ZIP signature probing (`PK` magic) for initial format validation.
  - Diagnostic reporting for missing parser stages.

- `tools/avatar_tool.cpp`
  - New runtime API sanity tool.
  - Exercises `nativecore.dll` instead of direct facade calls.
  - Prints normalized format/compatibility/diagnostic information.

### Changed

- `include/vsfclone/avatar/avatar_package.h`
  - Added `AvatarSourceType::VxAvatar`.
  - Added `AvatarCompatLevel` enum.
  - Added `compat_level` field.
  - Added `missing_features` list.

- `src/avatar/avatar_loader_facade.cpp`
  - Registered `VxAvatarLoader` in extension dispatch chain.

- `src/avatar/vrm_loader.cpp`
  - Added compatibility/missing-feature diagnostics for scaffold state.

- `src/avatar/vsfavatar_loader.cpp`
  - Added compatibility classification.
  - Added explicit pending-feature diagnostics for UnityFS deep parse path.

- `src/main.cpp`
  - Added `VXAvatar` source type display support.
  - Replaced non-ASCII usage sample path with ASCII-safe sample path.

- `CMakeLists.txt`
  - Converted `vsfclone_core` to static internal library.
  - Added shared library target: `nativecore`.
  - Added executable target: `avatar_tool`.
  - Wired include paths and export macro definition for DLL build.

- `build.ps1`
  - Updated build output summary to include `nativecore.dll` and `avatar_tool.exe`.

- `README.md`
  - Updated current capabilities.
  - Documented API and runtime scope.
  - Added implementation summary and verification notes.

### Verified

- CMake configure + MSVC Release build succeeded.
- Built artifacts produced:
  - `build/Release/nativecore.dll`
  - `build/Release/vsfclone_cli.exe`
  - `build/Release/avatar_tool.exe`
- `avatar_tool.exe` tested with a real `.vsfavatar` file:
  - Load success
  - Detected format: `VSFAvatar`
  - Compatibility: `partial`
  - Missing-feature diagnostics returned as expected

### Known gaps after this update

- DX11 renderer is not implemented yet (render call is placeholder).
- VRM decode + MToon binding are not implemented.
- `.vxavatar` manifest/material override parser is not implemented.
- `.vsfavatar` deep object extraction is not implemented.
- MediaPipe webcam tracking integration is not implemented.
- WinUI/WPF host app project is not created yet.
