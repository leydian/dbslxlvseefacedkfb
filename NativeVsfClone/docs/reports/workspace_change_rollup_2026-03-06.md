# Workspace Change Rollup (2026-03-06)

## Summary

This report consolidates the full workspace update set currently staged on `2026-03-06` with emphasis on:

- XAV2/lilToon render-breakage mitigation and v3 skinning path uplift
- Unity XAV2 importer path addition (menu + importer + editor tests)
- host UI copy/encoding stabilization (WPF + WinUI)
- release/build guardrail expansion for text encoding and render regression checks

The current delta includes both runtime/format changes and host/operator usability hardening.

## Change Set Snapshot

### Modified files (tracked)

- `NativeVsfClone/build.ps1`
- `NativeVsfClone/docs/formats/xav2.md`
- `NativeVsfClone/host/WinUiHost/App.xaml`
- `NativeVsfClone/host/WinUiHost/App.xaml.cs`
- `NativeVsfClone/host/WinUiHost/MainWindow.xaml`
- `NativeVsfClone/host/WinUiHost/MainWindow.xaml.cs`
- `NativeVsfClone/host/WpfHost/App.xaml`
- `NativeVsfClone/host/WpfHost/App.xaml.cs`
- `NativeVsfClone/host/WpfHost/MainWindow.xaml`
- `NativeVsfClone/host/WpfHost/MainWindow.xaml.cs`
- `NativeVsfClone/host/WpfHost/RenderHwndHost.cs`
- `NativeVsfClone/include/vsfclone/avatar/avatar_package.h`
- `NativeVsfClone/src/avatar/xav2_loader.cpp`
- `NativeVsfClone/src/nativecore/native_core.cpp`
- `NativeVsfClone/tools/xav2_render_regression_gate.ps1`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Editor/Xav2Exporter.cs`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/README.md`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

### Added files (new)

- `.editorconfig`
- `.gitattributes`
- `NativeVsfClone/tools/check_ui_encoding.ps1`
- `NativeVsfClone/tools/avatar_load_soak_gate.ps1`
- `NativeVsfClone/tools/render_perf_gate.ps1`
- `NativeVsfClone/tools/vsfavatar_gated_trend.ps1`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Editor/Xav2ImportMenu.cs`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Editor/Xav2Importer.cs`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Editor/Xav2ImporterTypes.cs`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Tests/Editor/VsfClone.Xav2.Editor.Tests.asmdef`
- `NativeVsfClone/unity/Packages/com.vsfclone.xav2/Tests/Editor/Xav2ImporterTests.cs`

## Detailed Changes

### 1) XAV2 v3 render/format path (lilToon breakage line)

Updated:

- `include/vsfclone/avatar/avatar_package.h`
- `src/avatar/xav2_loader.cpp`
- `src/nativecore/native_core.cpp`
- `docs/formats/xav2.md`
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`
- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

Key updates:

- Added XAV2 `v3` handling and section `0x0016` (`Skeleton pose payload`) on native + Unity runtime loader paths.
- Expanded avatar/runtime data model with `skeleton_payloads` / `Xav2SkeletonPayload`.
- Strengthened skeleton diagnostics contract:
  - `XAV3_SKELETON_PAYLOAD_MISSING`
  - `XAV3_SKELETON_MESH_BIND_MISMATCH`
  - `XAV3_SKINNING_MATRIX_INVALID`
- Native skinning path now prioritizes skeleton-aware matrix application (`bone matrix + bindpose`) when v3 skeleton payload is valid.
- Typed material (`typed-v2`) path remains active with normalized texture reference handling and compatibility warnings.
- Format document updated to include v3 version and `0x0016` section schema.

### 2) Unity exporter/importer workflow expansion

Updated:

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2Exporter.cs`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2AvatarExtractors.cs`
- `unity/Packages/com.vsfclone.xav2/README.md`

Added:

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2ImportMenu.cs`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2Importer.cs`
- `unity/Packages/com.vsfclone.xav2/Editor/Xav2ImporterTypes.cs`
- `unity/Packages/com.vsfclone.xav2/Tests/Editor/Xav2ImporterTests.cs`
- `unity/Packages/com.vsfclone.xav2/Tests/Editor/VsfClone.Xav2.Editor.Tests.asmdef`

Key updates:

- Export container version moved to `3`.
- Export now emits skeleton pose section `0x0016` and validates v3 skin/skeleton coverage before writing:
  - skeleton payload must exist for skinned mesh
  - matrix array must be valid (`16`-multiple)
  - skeleton matrix count must cover bindpose count
- Added Unity editor import command:
  - `Tools/VsfClone/XAV2/Import XAV2...`
- Implemented importer pipeline:
  - load via runtime loader
  - create texture/material/mesh assets
  - rebuild prefab hierarchy with MeshRenderer/SkinnedMeshRenderer
  - preserve partial-import warning channel in report
- Added editor tests for importer creation and duplicate import collision behavior.

### 3) Host UI text/encoding stabilization (WPF + WinUI)

Updated:

- `host/WpfHost/*.xaml`, `host/WpfHost/*.cs`
- `host/WinUiHost/*.xaml`, `host/WinUiHost/*.cs`

Key updates:

- Replaced garbled/mojibake UI strings with normalized Korean-first labels and bilingual operational prompts.
- Updated confirmation and validation messages for clearer operator decisions.
- Introduced UTF-8 BOM normalization on host UI source files to reduce encoding drift.

### 4) Build/release guardrail updates

Updated:

- `build.ps1`
- `tools/xav2_render_regression_gate.ps1`

Added:

- `tools/check_ui_encoding.ps1`
- `tools/avatar_load_soak_gate.ps1`
- `tools/render_perf_gate.ps1`
- `tools/vsfavatar_gated_trend.ps1`
- `.editorconfig`
- `.gitattributes`

Key updates:

- `build.ps1` now supports `-ValidateUiEncoding` to run host UI text/encoding checks pre-build.
- New `check_ui_encoding.ps1` detects:
  - missing UTF-8 BOM in UI files
  - likely mojibake patterns in XAML strings
- Render regression gate expanded:
  - treats new v3 skeleton warning codes as critical in strict mode
  - optional snapshot diff gate (`UnitySnapshotDir`, `NativeSnapshotDir`, threshold, strict fail switch)
  - summary includes GateX5 and per-image diff rows.
- Additional release-quality helper gates added:
  - avatar load soak gate (`avatar_load_soak_gate.ps1`)
  - render performance percentile/drop gate (`render_perf_gate.ps1`)
  - VSFAvatar gate trend summarizer (`vsfavatar_gated_trend.ps1`)
- Repository text/encoding defaults standardized via `.editorconfig` + `.gitattributes`.

## Verification Snapshot

Executed commands in this workspace:

```powershell
cmake --build NativeVsfClone\build --config Release --target nativecore avatar_tool
powershell -ExecutionPolicy Bypass -File NativeVsfClone\tools\xav2_render_regression_gate.ps1 `
  -SampleDir D:\dbslxlvseefacedkfb `
  -AvatarToolPath D:\dbslxlvseefacedkfb\NativeVsfClone\build\Release\avatar_tool.exe `
  -FailOnRenderWarnings
NativeVsfClone\build\Release\vsfclone_cli.exe "D:\dbslxlvseefacedkfb\개인작11-3.xav2"
```

Observed:

- native build (`nativecore`, `avatar_tool`): PASS
- render regression gate (`GateX1..X5`): PASS on current sample set
- sample parse route: `runtime-ready`, `PrimaryError=NONE`

Known caveat:

- Unity EditMode test execution and full screenshot-comparison operation still require Unity batch runner/context in target environment.
