# Feature Gate/UI Alignment for Arm, Realtime Shadow, and Expression (WPF + WinUI) (2026-03-06)

## Summary

Implemented a host-side feature gate resolver and aligned UI enablement logic to eliminate the mismatch where controls appeared enabled but the underlying feature was policy-gated or unavailable.

Target symptom cluster:

- arm controls appear active but no movement is applied
- realtime shadow controls appear active but shadow path is unavailable
- facial expression path appears active while tracking/payload gate blocks submission

This pass focuses on operator-visible correctness and deterministic reason surfacing, not runtime capability expansion.

## Problem Context

Prior UI availability was mostly tied to session/busy state (`RenderControlsEnabled`) and did not include per-feature capability/policy signals.

Result:

- controls were interactable even when runtime/avatar/tracking preconditions failed
- operators experienced "button active, feature inactive"
- root-cause extraction required manual correlation from multiple diagnostics fields

## Implementation

### 1) Centralized feature gate resolver

Added:

- `host/HostCore/HostFeatureGates.cs`

New contracts:

- `FeatureGateState` (`Enabled`, `ReasonCode`, `ReasonText`)
- `FeatureGateSnapshot` (common class/reason + per-feature states)
- `HostFeatureGateResolver.Evaluate(...)`

Priority model:

1. runtime binary mismatch/stale
2. native submit failure (`NC_SET_*`)
3. avatar payload/policy gate (arm/shadow/expression signals)
4. tracking inactive/stale (expression path)

Per-feature rules:

- Arm:
  - disabled on `ARM_POSE_*` warning signals
  - disabled for non-MIQ format (`ARM_POSE_FORMAT_UNSUPPORTED`)
- Realtime shadow:
  - disabled on `SHADOW_DISABLED_*` signals
  - disabled when active passes do not report shadow (`SHADOW_PASS_NOT_REPORTED`)
- Expression:
  - disabled on native submit failure (`NC_SET_*`)
  - disabled when avatar expression payload is absent (`EXPRESSION_COUNT_ZERO`)
  - disabled when tracking is inactive/stale (`TRACKING_INACTIVE`/`TRACKING_STALE`)

### 2) UI availability contract expansion

Updated:

- `host/HostCore/HostUiPolicy.cs`

`HostUiAvailability` was extended with:

- `ArmPoseEnabled`, `ArmPoseReasonCode`
- `RealtimeShadowEnabled`, `RealtimeShadowReasonCode`
- `ExpressionEnabled`, `ExpressionReasonCode`

`EvaluateAvailability(...)` now accepts runtime/avatar/tracking context and derives feature-level availability from the shared resolver.

### 3) WPF wiring (control state + reason visibility)

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Changes:

- arm controls are now gated by `ArmPoseEnabled` (not global render-only gate)
- shadow controls are now gated by `RealtimeShadowEnabled`
- disabled controls expose reason code via tooltip
- tracking start button shows expression gate warning tooltip
- runtime/avatar diagnostics include `FeatureGate` lines
- `CommonCauseTriage` line now uses the shared resolver output for consistency

### 4) WinUI parity wiring

Updated:

- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- shadow controls are now gated by `RealtimeShadowEnabled`
- disabled shadow controls expose reason code via tooltip
- tracking start button shows expression gate warning tooltip
- runtime/avatar diagnostics include `FeatureGate` lines using the shared resolver

## Behavioral Result

UI now reflects actual feature readiness instead of only global session/busy readiness.

Expected operator behavior:

1. if feature is unavailable, control is disabled
2. reason code is visible immediately on hover/diagnostics
3. runtime diagnostics expose a single common class/reason plus per-feature gate details

This removes the primary UX confusion loop ("control is enabled but feature is not active").

## Verification

Build verification:

- `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore`: PASS
- `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore`: PASS
- `dotnet build host/WinUiHost/WinUiHost.csproj -c Release --no-restore`: BLOCKED
  - `NU1301`: missing local source `D:\dbslxlvseefacedkfb\NativeVsfClone\build\nuget-mirror`

No native ABI/API change was introduced in this pass.

## Risk / Compatibility

- Behavior change is intentional for UI actionability:
  - some controls may be disabled more often than before, but now with explicit reason codes
- runtime behavior is unchanged unless previously hidden by UI mismatch
- WinUI compile verification remains environment-blocked by local NuGet mirror dependency
