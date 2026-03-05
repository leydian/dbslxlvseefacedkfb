# XAV2 Visibility Fallback and Warning-Gate Update (2026-03-06)

## Summary

This update addresses the "load succeeds but avatar not visible" troubleshooting line for `.xav2` by hardening native v3 skinning validation/fallback and improving warning diagnostics visibility in both tooling and host UI.

Primary outcomes:

- native render path now falls back to legacy static skinning when v3 skeleton payload is missing/invalid per mesh
- skeleton validity checks now reject non-finite matrix payloads
- `avatar_tool` now exposes all warning codes, not only the last code
- XAV2 render regression gate now evaluates full warning-code set for critical warning detection
- WPF Avatar diagnostics now surfaces warning count and parsed last warning code directly

## Problem Context

Observed operator symptom:

- `.xav2` file loads to `runtime-ready` with `PrimaryError=NONE`
- render panel may remain effectively blank/invisible
- prior gate logic could miss critical warnings if they were not the *last* warning code

Risk line:

- if v3 skeleton payload becomes missing/invalid for a skinned mesh, render path could skip effective skinning and produce non-usable geometry presentation
- warning visibility in tools/gates/UI was insufficient for fast root-cause isolation

## Implementation Details

### 1) Native render skinning fallback hardening

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- `IsValidSkeletonPosePayload(...)`
  - keeps existing shape/count checks
  - adds finite-value validation for:
    - `skin_payload.bind_poses_16xn`
    - `skeleton_payload.bone_matrices_16xn`
- `BuildGpuMeshForPayload(...)`
  - added `force_static_skinning_fallback` argument
  - behavior:
    - valid skeleton -> apply skeleton-aware static skinning path
    - invalid/missing skeleton (forced fallback) -> apply legacy static skinning (`bindpose inverse`) path
    - env opt-in (`VSFCLONE_XAV2_ENABLE_STATIC_SKINNING`) still works as before
- `EnsureAvatarGpuMeshes(...)`
  - when `XAV3_SKINNING_MATRIX_INVALID` or `XAV3_SKELETON_PAYLOAD_MISSING` is detected for a mesh, fallback flag is set for that mesh build

Intent:

- avoid "no useful deformation path" in v3 warning scenarios
- keep warning signal while preserving a visible fallback render path

### 2) Tooling diagnostics expansion

Updated:

- `tools/avatar_tool.cpp`

Changes:

- now prints all warning codes:
  - `WarningCode[0]: ...`
  - `WarningCode[1]: ...`
  - ...
- existing summary fields (`Warnings`, `WarningCodes`, `LastWarningCode`, `LastWarning`) are preserved

### 3) Gate logic correction (full warning-set check)

Updated:

- `tools/xav2_render_regression_gate.ps1`

Changes:

- parser now collects all `WarningCode[n]` entries
- strict warning gate (`GateX4`) now checks critical code presence across full set:
  - `XAV2_SKINNING_STATIC_DISABLED`
  - `XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED`
  - `XAV3_SKELETON_PAYLOAD_MISSING`
  - `XAV3_SKELETON_MESH_BIND_MISMATCH`
  - `XAV3_SKINNING_MATRIX_INVALID`
- row summary now emits:
  - `warning_codes=<comma-separated-list>`
  - `last_warning_code=<...>`

### 4) WPF diagnostics visibility improvement

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Changes:

- Avatar diagnostics text now includes:
  - `WarningCount`
  - `LastWarningCode` (parsed from `LastWarning`)
- added helper:
  - `ExtractWarningCode(string warningText)`

## Verification Snapshot

Executed:

```powershell
cmake --build NativeVsfClone\build --config Release --target nativecore
cmake --build NativeVsfClone\build --config Release --target avatar_tool
NativeVsfClone\build\Release\avatar_tool.exe "D:\dbslxlvseefacedkfb\개인작11-3.xav2"
powershell -ExecutionPolicy Bypass -File NativeVsfClone\tools\xav2_render_regression_gate.ps1 `
  -SampleDir D:\dbslxlvseefacedkfb `
  -AvatarToolPath D:\dbslxlvseefacedkfb\NativeVsfClone\build\Release\avatar_tool.exe `
  -SummaryPath D:\dbslxlvseefacedkfb\build\reports\xav2_render_regression_gate_summary.txt `
  -FailOnRenderWarnings
```

Observed:

- `nativecore`: build PASS
- `avatar_tool`: build PASS
- `avatar_tool` output includes `WarningCode[n]` entries as expected
- updated gate summary row now shows full `warning_codes=...` field

Current environment note:

- `dotnet build host/WpfHost/WpfHost.csproj -c Release` is currently blocked in this workspace by pre-existing host dependency/reference issues unrelated to this patch line.

## Scope Notes

- This update focuses on visibility-stability and diagnostics correctness for XAV2 runtime/gate/tool/host diagnostics.
- It does not attempt full lilToon parity expansion or host tracking dependency remediation.
