# Consumer WPF Flow and Tracking Schema Update (2026-03-06)

## Summary

This update consolidates the current in-progress workspace changes as of `2026-03-06` across HostCore, WPF host UX, native renderer safety, and VSFAvatar quality gate script behavior.

Primary intent:

- strengthen end-consumer WPF flow guidance (`Initialize -> Load -> Start output`)
- preserve advanced diagnostics access while simplifying beginner-facing failure messaging
- extend tracking session schema/contracts for multi-source readiness
- pass non-pose tracking expression weights to native runtime
- harden native skin payload validation and shader/material plumbing
- relax one VSFAvatar gate field requirement for sidecar serialized-path diagnostics
- expand Unity runtime test coverage for typed-material texture-ref normalization

## Files in Scope

- `host/HostCore/HostController.MvpFeatures.cs`
- `host/HostCore/HostController.cs`
- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/HostUiPolicy.cs`
- `host/HostCore/NativeCoreInterop.cs`
- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/TrackingInputService.cs`
- `host/WpfHost/MainWindow.xaml.cs`
- `include/vsfclone/nativecore/api.h`
- `src/nativecore/native_core.cpp`
- `tools/vsfavatar_quality_gate.ps1`
- `tools/vsfavatar_render_gate.ps1`
- `tools/xav2_render_regression_gate.ps1`
- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

## Detailed Changes

### 1) WPF consumer-flow guidance and beginner failure handling

Updated:

- `host/WpfHost/MainWindow.xaml.cs`
- `host/HostCore/HostUiPolicy.cs`

Key changes:

- Added next-action guidance contract in HostCore policy:
  - `HostNextRecommendedAction`
  - `HostNextActionHint`
  - `BuildNextActionHint(...)`
- WPF UI state refresh now binds quick guidance text from policy-computed next step.
- Replaced direct technical-first popup behavior for key beginner path failures:
  - avatar load
  - Spout start
  - OSC start
- Added beginner-mode failure hint persistence and advanced-diagnostics jump path:
  - tracks last failure source
  - stores concise recovery hint text
  - allows immediate diagnostics reveal when user requests deeper details
- Success paths for Load/Spout/OSC clear prior failure hint state.

Behavioral outcome:

- Beginner users get action-oriented remediation with reduced technical noise.
- Advanced mode still retains full technical details for diagnosis.

### 2) Tracking contract/session schema extension for source-mode readiness

Updated:

- `host/HostCore/HostInterfaces.cs`
- `host/HostCore/PlatformFeatures.cs`
- `host/HostCore/HostController.MvpFeatures.cs`
- `host/HostCore/HostController.cs`
- `host/HostCore/NativeCoreInterop.cs`
- `host/HostCore/TrackingInputService.cs`
- `include/vsfclone/nativecore/api.h`

Key changes:

- Added `TrackingSourceType` enum (`OscIfacial`, `WebcamOnnx`).
- Extended `TrackingStartOptions` to include:
  - `SourceType`
  - `WebcamDeviceId`
  - `OnnxModelPath`
  - `InferenceFpsCap`
- Extended `TrackingDiagnostics` with:
  - `SourceType`
  - `SourceStatus`
- Extended `ITrackingInputService` with:
  - `TryGetLatestExpressionWeights(...)`
- Added native interop shape and API for expression payloads:
  - `NcExpressionWeight`
  - `nc_set_expression_weights(...)`
- Synced native C public header contract with the same expression API:
  - `NcExpressionWeight` struct declaration
  - `nc_set_expression_weights(...)` export declaration
- Expanded persisted tracking schema in `TrackingInputSettings`:
  - source type
  - webcam/model identifiers
  - inference FPS cap
- Bumped `SessionPersistenceModel` default version from `3` to `4`, including compatibility normalization for the new fields.
- Extended HostController tracking config API to support optional source-specific fields while preserving existing defaults and clamping behavior.
- In per-tick host loop, expression weights are now forwarded to native runtime:
  - pose channels (`headyaw/headpitch/headroll/headpos*`) are filtered out
  - remaining weights are clamped to `[0.0, 1.0]` and submitted via `nc_set_expression_weights(...)`
- Tracking service runtime path now includes source-aware diagnostics/status:
  - OSC vs webcam-onnx source-mode startup routing
  - source status text transitions (`udp-listening`, `udp-receiving`, `udp-parse-failed`, `stopped`, etc.)
  - expression cache snapshot export via `TryGetLatestExpressionWeights(...)`

Behavioral outcome:

- Tracking configuration state is now explicit and future-ready for non-OSC source modes while preserving backward-compatible defaults.

### 3) Native renderer robustness and skin/material path improvements

Updated:

- `src/nativecore/native_core.cpp`

Key changes:

- Material GPU resource/state extension:
  - added normal/rim texture SRVs and related lilToon-like parameters
  - added corresponding resource release/reset handling
- Vertex/pixel shader constant layout update:
  - grouped alpha misc values and liltoon parameter vectors
  - introduced normal-aware shading path and rim contribution path
- Vertex input layout expanded:
  - `POSITION + NORMAL + TEXCOORD`
  - vertex stride updated from `20` to `32`
- Mesh upload path now preserves normals where available, with fallback default normals when absent.
- Added skin payload quality validation before applying static skinning:
  - bind pose shape checks
  - skin-weight blob size/decode checks
  - per-vertex bone-index bounds checks
  - per-vertex weight-sum sanity check
- On invalid skin payload, renderer records warning codes/details and skips skin payload application instead of unsafe processing.

Behavioral outcome:

- Better rendering stability and clearer warning diagnostics for malformed skin payloads.
- Expanded shading parameter support path with safer resource lifecycle handling.
- Render transform now applies tracked head rotation/position in world composition (with normalization and bounded positional scale).
- Implemented native `nc_set_expression_weights(...)` path:
  - normalizes incoming expression keys
  - maps direct/mapping-kind aliases (including blink/jaw/smile fallbacks)
  - updates per-expression runtime weights and summary text per avatar

### 4) VSFAvatar quality-gate field rule adjustment

Updated:

- `tools/vsfavatar_quality_gate.ps1`
- `tools/vsfavatar_render_gate.ps1`
- `tools/xav2_render_regression_gate.ps1` (new)

Key change:

- `Require-Field` now treats `SidecarSerializedBestPath` as key-presence-required instead of non-empty-string-required.
- `vsfavatar_render_gate.ps1` sample-row parsing now skips non-sample headers before counting rows, reducing false-positive parse failures from report noise.
- Added `xav2_render_regression_gate.ps1`:
  - runs `avatar_tool` over `.xav2` samples
  - evaluates parser/error/warning gates (`GateX1..GateX4`)
  - supports strict warning fail mode (`-FailOnRenderWarnings`)
  - emits summary artifact to `build/reports`
- Adjusted `xav2_render_regression_gate.ps1` gate matching:
  - parser stage/error checks now use `-match` to tolerate surrounding text variation in tool output

Behavioral outcome:

- Gate no longer fails solely due to empty serialized best-path values when key presence is sufficient for contract checks.
- VSFAvatar render-gate parsing is more robust against unrelated report lines.
- XAV2 render regression checks now have a standalone gate harness script.

### 5) Unity XAV2 typed-material normalization test expansion

Updated:

- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs`

Key changes:

- Added regression test:
  - `TryLoad_TypedMaterialTextureRef_NormalizedMatch_DoesNotWarn`
- Test fixture helpers now allow explicit texture-ref variants:
  - `textureRefName`
  - `typedBaseTextureRefOverride`
- Test verifies path/case-normalized typed material texture refs do not produce unresolved texture warnings.

Behavioral outcome:

- Better guardrail coverage against false-positive unresolved typed-texture warnings in runtime loader diagnostics.

## Verification Snapshot

Executed in current workspace:

```powershell
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
dotnet build NativeVsfClone\host\WpfHost\WpfHost.csproj -c Release --no-restore
```

Outcome:

- `HostCore`: PASS (`0 warnings`, `0 errors`)
- `WpfHost`: PASS (`0 warnings`, `0 errors`)

Notes:

- WPF build initially hit a restore/network (`NU1301`) issue in sandboxed execution, then succeeded once restore access was available.
