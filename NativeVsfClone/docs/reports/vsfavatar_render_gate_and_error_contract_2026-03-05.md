# VSFAvatar Render Gate + Error Contract Update (2026-03-05)

## Summary

This round focused on two concrete outcomes:

1. Enable a renderable VSFAvatar path for at least one fixed sample by extending the sidecar-to-loader contract.
2. Make host-side load failure diagnostics preserve load-context details instead of collapsing into generic `Unsupported` messaging.

## Implemented Changes

### 1) Sidecar schema extension (v4)

- updated:
  - `tools/vsfavatar_sidecar.cpp`
- contract updates:
  - `schema_version`: `4`
  - `extractor_version`: `inhouse-sidecar-v4`
  - added fields:
    - `render_payload_mode`
    - `mesh_payload_count`
    - `material_payload_count`
- behavior:
  - when probe stage is `complete` with `object_table_parsed=true` and discovered mesh count is `0`, sidecar now emits:
    - `render_payload_mode=placeholder_quad_v1`
    - payload count hints for one placeholder mesh/material path

### 2) VSFAvatar loader payload bridging

- updated:
  - `src/avatar/vsfavatar_loader.cpp`
- behavior:
  - accepts sidecar schema `2/3/4`
  - when `render_payload_mode=placeholder_quad_v1`, constructs:
    - placeholder quad `MeshRenderPayload`
    - placeholder `MaterialRenderPayload`
  - if no mesh payload is available and compat is not failed:
    - sets `primary_error_code=VSF_MESH_PAYLOAD_MISSING`
    - appends corresponding warning/missing-feature guidance

### 3) Host load-failure diagnostic preservation

- updated:
  - `host/HostCore/HostInterfaces.cs`
  - `host/HostCore/AvatarSessionService.cs`
  - `host/HostCore/HostController.cs`
  - `host/HostCore/HostController.MvpFeatures.cs`
  - `host/WpfHost/MainWindow.xaml.cs`
  - `host/WinUiHost/MainWindow.xaml.cs`
- behavior:
  - `AvatarSessionService` now retains `LastLoadAttemptInfo` from successful native load prior to render-resource step.
  - `HostController` stores load-failure guidance + technical detail (`GetLastLoadFailureDetails()`).
  - `Unsupported` classification is refined:
    - load/render-resource unsupported paths are treated as runtime/asset support issues, not generic toolchain failure.
  - WPF/WinUI load failure popups prefer the preserved load-failure details.

### 4) New VSFAvatar render gate

- added:
  - `tools/vsfavatar_render_gate.ps1`
- updated:
  - `tools/run_quality_baseline.ps1`
- gate contract:
  - `GateR1`: at least one fixed-set sample reports `MeshPayloads > 0`
  - `GateR2`: `ParserStage=complete` rows have non-empty `PrimaryError`
  - integrated into baseline script as `VSFAvatar render gate`

## Verification

Executed in this round:

```powershell
cmake --build NativeVsfClone\build --config Release --target vsfavatar_sidecar nativecore avatar_tool
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore -nr:false
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_render_gate.ps1 -UseFixedSet
```

Results:

- C++ targets (`vsfavatar_sidecar`, `nativecore`, `avatar_tool`): PASS
- HostCore build: PASS (one transient file-lock retry warning observed)
- WPF host build: PASS
- VSFAvatar render gate: PASS
  - report: `build/reports/vsfavatar_render_gate_summary.txt`
  - key metrics:
    - `SampleCount: 4`
    - `renderable_mesh_payload_rows: 1`
    - `Overall: PASS`

## Notes

- This is a v1 renderability bridge (placeholder payload path) to unblock end-to-end runtime flow.
- Full authored mesh/material extraction for VSFAvatar remains a follow-up track.
