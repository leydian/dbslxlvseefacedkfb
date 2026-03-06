# Workspace Change Rollup (2026-03-05)

## Summary

This rollup consolidates all current workspace changes into a single implementation snapshot.

Main themes:

- Unity MIQ SDK support hardening:
  - minimum support floor expanded to Unity `2021.3.18f1+`
  - self-hosted CI compatibility gate (`EditMode tests + export/load smoke`)
- VRM runtime quality expansion:
  - expression bind visibility and runtime morph deformation path
  - SpringBone metadata extraction and diagnostics surface
- Host operational reliability hardening (WPF-first):
  - output-state mismatch detection and bounded auto-recovery
  - repeatable WPF reliability loop gate automation
- Documentation and quality artifact updates aligned with the above changes

## 1) Runtime / Native API / Loader changes

### VRM model and loader expansion

Updated:

- `include/animiq/avatar/avatar_package.h`
- `src/avatar/vrm_loader.cpp`

Key additions:

- `ExpressionState::Bind` list for expression-to-mesh/frame mapping.
- `SpringBoneSummary` in `AvatarPackage`.
- VRM loader now extracts:
  - morph target position deltas
  - expression binds from VRM1 (`VRMC_vrm`) and VRM0.x (`VRM.blendShapeMaster`)
  - SpringBone summary from VRM1 (`VRMC_springBone`) and VRM0.x (`secondaryAnimation`)
- diagnostics/missing-feature messaging updated accordingly.

### NativeCore C API surface extension

Updated:

- `include/animiq/nativecore/api.h`
- `src/nativecore/native_core.cpp`

Added types:

- `NcExpressionInfo`
- `NcSpringBoneInfo`

Added APIs:

- `nc_get_expression_count`
- `nc_get_expression_infos`
- `nc_get_springbone_info`

Compatibility:

- additive extension only; existing `NcAvatarInfo` and existing C API calls remain intact.

### Runtime morph application path

Updated:

- `src/nativecore/native_core.cpp`

Behavior:

- GPU mesh vertex buffers switched to dynamic write usage for runtime deformation.
- expression runtime weights are applied to blendshape deltas per mesh/frame bind.
- deformed vertex blobs are uploaded before draw in render frame path.

## 1.1) Unity SDK support and gate automation

Updated / added:

- `unity/Packages/com.animiq.miq/package.json`
- `unity/Packages/com.animiq.miq/README.md`
- `tools/unity_miq_validate.ps1`
- `unity/Packages/com.animiq.miq/Editor/MiqCiSmoke.cs`
- `.github/workflows/unity-miq-compat.yml`

Behavior:

- package minimum Unity changed to `2021.3` + `unityRelease=18f1`.
- compatibility support is now operationalized as executable checks:
  - EditMode test run
  - MIQ export smoke
  - MIQ load smoke (`runtime-ready`)
- CI workflow runs on self-hosted Windows and uploads validation artifacts.

## 2) Host / Tooling / Reliability changes

### Host output-state reconciliation (WPF-first operational safety)

Updated:

- `host/HostCore/HostController.cs`

Behavior:

- tracks desired output intent (`Spout`, `OSC`) independently.
- compares host/UI output state with runtime stats every tick.
- emits throttled mismatch diagnostics.
- performs bounded reconciliation attempts (start/stop with last-known settings).

### Host interop and diagnostics consumers

Updated:

- `host/HostCore/NativeCoreInterop.cs`
- `tools/avatar_tool.cpp`

Behavior:

- HostCore interop now includes expression/springbone native calls and structs.
- `avatar_tool` now prints:
  - `ExpressionBindTotal`
  - SpringBone summary fields (`present`, springs/joints/colliders/collider_groups)

### WPF reliability loop gate

Added:

- `tools/wpf_reliability_gate.ps1`

Behavior:

- runs WPF publish + launch smoke in loop (`Iterations`).
- optional baseline pass at end.
- aggregates status/timing/failures into:
  - `build/reports/wpf_reliability_gate_latest.txt`

## 3) Quality Gate and Generated Evidence updates

### VRM quality gate contract expanded

Updated:

- `tools/vrm_quality_gate.ps1`

New checks:

- GateE: at least one sample has `ExpressionBindTotal > 0`
- GateF: SpringBone visibility contract (`SpringBonePresent` in `{true,false}`)

Overall pass now requires `GateA..GateF`.

### Updated generated artifacts

Updated:

- `build/reports/vrm_probe_fixed5.txt`
- `build/reports/vrm_gate_fixed5.txt`

Notable differences:

- probe now includes `ExpressionBindTotal` + SpringBone summary fields per sample.
- gate summary includes new GateE/GateF pass lines.

## 4) Documentation updates

Updated:

- `README.md`
  - host capability note for runtime output-state reconciliation
  - WPF reliability loop command and report output
  - Unity MIQ SDK support contract (`2021.3.18f1` gate-backed support)
- `CHANGELOG.md`
  - VRM expression/springbone + runtime morph + API extension entry
  - Unity support-floor and gate automation entries
- `docs/INDEX.md`
  - added links for latest reliability and rollup reports
  - added Unity support/gate report link
- added report:
  - `docs/reports/wpf_operational_reliability_loop_and_output_sync_2026-03-05.md`
  - `docs/reports/miq_unity_2021_3_18f1_support_and_gate_2026-03-05.md`
  - this rollup (`workspace_change_rollup_2026-03-05.md`)

## Validation Snapshot

Executed during this workspace update:

```powershell
dotnet build NativeAnimiq\host\HostCore\HostCore.csproj -c Release
dotnet build NativeAnimiq\host\WpfHost\WpfHost.csproj -c Release
powershell -ExecutionPolicy Bypass -File NativeAnimiq\tools\wpf_reliability_gate.ps1 -Iterations 1 -SkipNativeBuild
cmake --build NativeAnimiq\build --config Release
cd NativeAnimiq; powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5
```

Outcome:

- HostCore build: PASS
- WPF host build: PASS
- WPF reliability gate (1 loop): PASS (with WPF launch smoke)
- Native build: PASS
- VRM quality gate fixed5: PASS (GateA..GateF)

## Commit snapshot

- Latest consolidated implementation commit in this line:
  - `5d94d73` (`feat: roll up VRM runtime expansion and WPF reliability hardening`)
- This document update commit is a docs-only follow-up summary refresh.
