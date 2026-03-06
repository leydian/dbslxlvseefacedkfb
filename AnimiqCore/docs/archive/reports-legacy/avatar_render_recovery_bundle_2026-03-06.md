# Avatar Render Recovery Bundle (2026-03-06)

## Scope

This report captures the cumulative implementation changes applied during the avatar render recovery cycle across native runtime, host integration, Unity MIQ package, release automation, and documentation.

## Problem Statements Addressed

1. `.miq` avatar loaded as `runtime-ready` but rendered invisible.
2. `.vrm` avatar rendered with severe material/visibility corruption.
3. Distribution confusion caused by partial publish output (`exe/pdb/dll` only) and inconsistent startup behavior.
4. Diagnostics were insufficient to distinguish parser-ready state from render-contract mismatches.

## Implemented Fixes

### 1) Runtime tracking safety and neutral fallback

- files:
  - `host/HostCore/TrackingInputService.cs`
  - `host/HostCore/HostController.cs`
  - `src/nativecore/native_core.cpp`
- changes:
  - stale tracking now explicitly resets to neutral pose.
  - no-frame path submits neutral frame to native core.
  - native tracking ingest sanitizes invalid/quaternion payloads before render use.

### 2) MIQ render visibility and diagnostics hardening

- files:
  - `src/nativecore/native_core.cpp`
  - `host/WpfHost/MainWindow.xaml.cs`
  - `tools/avatar_tool.cpp`
  - `tools/miq_render_regression_gate.ps1`
- changes:
  - MIQ preview path applies no-cull fallback where required.
  - warning-code extraction in host/tooling fixed to avoid losing actionable codes.
  - regression gate now validates full warning-code set instead of single tail entry.

### 3) VRM mesh payload completeness

- file:
  - `src/avatar/vrm_loader.cpp`
- changes:
  - mesh extraction changed from first primitive only to all primitives.
  - multi-primitive naming and payload generation updated accordingly.
- effect:
  - missing draw-call/mesh payload regressions on multi-primitive VRM assets resolved.

### 4) VRM material parameter wiring uplift

- file:
  - `src/avatar/vrm_loader.cpp`
- changes:
  - added core typed/shader-params binding for base/shade/emission/rim channels.
  - added base/normal/emission/rim texture slot mappings.
  - added bump/rim scalar parameters used by current native shader path.

### 5) Alpha-mode contract fix (critical)

- files:
  - `src/avatar/vrm_loader.cpp`
  - `src/nativecore/native_core.cpp`
- root cause:
  - `_Cutoff` was emitted broadly, and resolver treated cutoff presence as `MASK`, forcing clipping on materials that should remain opaque.
- fix:
  - `_Cutoff` emission restricted to `alphaMode=MASK`.
  - native alpha resolver now avoids classifying `OPAQUE` as `MASK` from cutoff alone.
- effect:
  - opaque clothing/body material corruption significantly reduced.

### 6) Deployment and release automation reinforcement

- files:
  - `tools/release_gate_dashboard.ps1`
  - `tools/release_readiness_gate.ps1`
  - `tools/run_quality_baseline.ps1`
  - `tools/host_e2e_gate.ps1`
  - `tools/nuget_mirror_bootstrap.ps1`
  - `tools/sample_profile_resolve.ps1`
  - `tools/sidecar_lock_guard.ps1`
  - `tools/winui_xaml_min_repro.ps1`
  - `tools/session_state_migration_check/Program.cs`
  - `tools/tracking_parser_fuzz_gate/Program.cs`
- changes:
  - strengthened fail-fast release orchestration and supporting diagnostics tooling.
  - added reproducibility helpers for host publish and environment-dependent failures.

### 7) Unity MIQ package/runtime alignment

- files:
  - `unity/Packages/com.animiq.miq/Editor/MiqExportOptions.cs`
  - `unity/Packages/com.animiq.miq/Editor/MiqExporter.cs`
  - `unity/Packages/com.animiq.miq/Runtime/MiqDataModel.cs`
  - `unity/Packages/com.animiq.miq/Runtime/MiqRuntimeLoader.cs`
  - `unity/Packages/com.animiq.miq/Runtime/MiqLz4Codec.cs`
  - `unity/Packages/com.animiq.miq/Tests/Editor/MiqImporterTests.cs`
  - `unity/Packages/com.animiq.miq/Tests/Editor/MiqExporterTests.cs`
  - `unity/Packages/com.animiq.miq/Tests/Runtime/MiqRuntimeLoaderTests.cs`
- changes:
  - synchronized typed-v2 schema handling and loader/exporter parity with native side.
  - expanded edge/negative-path test coverage.

## Validation Snapshot

- native build:
  - `cmake --build .\\build --config Release --target nativecore` PASS
- runtime distribution:
  - `dist\\wpf\\nativecore.dll` refreshed from latest Release output.
- host smoke run:
  - `dist\\wpf\\WpfHost.exe` launch smoke check PASS.

## Remaining Known Gaps (Non-blocking for visibility fix)

1. SpringBone runtime simulation remains unimplemented in current runtime path.
2. Full MToon parity is not complete; current binding is a core subset for stabilization.
3. WinUI publish/toolchain reliability still has environment-specific blockers tracked in release board docs.

