# 2026-03-08 Plan Implementation Follow-up and Runtime Hardening

## Summary

This follow-up executes the requested "implement the plan" slice by productizing benchmark-derived behaviors into runtime paths and diagnostics visibility.

Implemented scope in this pass:

- Automation workflow trigger capability uplift (OSC wildcard + payload conditions)
- Spout2 receiver diagnostics surfaced in HostController and WPF diagnostics text
- Tracking/expression safety normalization and alias hard-clear policy refinement
- Arm-pose policy narrowing to MIQ and helper-bone fallback stabilization in native core

## Changed Files

- `AnimiqCore/host/HostCore/AutomationWorkflow.cs`
- `AnimiqCore/host/HostCore/HostController.Automation.cs`
- `AnimiqCore/host/WpfHost/MainWindow.xaml.cs`
- `AnimiqCore/host/HostCore/HostController.cs`
- `AnimiqCore/host/HostCore/TrackingInputService.cs`
- `AnimiqCore/host/HostCore/HostFeatureGates.cs`
- `AnimiqCore/src/nativecore/native_core.cpp`

## Detailed Changes

### 1) Automation workflow: richer OSC trigger matching

File: `AnimiqCore/host/HostCore/AutomationWorkflow.cs`

- Extended `OscTrigger` matching from strict address equality to predicate-based match:
  - wildcard address matching for `*` and `?`
  - optional numeric filters:
    - `float_min`
    - `float_max`
    - `float_equals`
  - optional string filters:
    - `string_equals`
    - `string_contains`
    - `string_ignore_case` (default `true`)
- Added invariant float parsing (`CultureInfo.InvariantCulture`) for deterministic automation graph behavior across locales.

Operational result:

- workflow JSON can now express event selection conditions without custom code,
- OSC-heavy setups can reduce false trigger activations and branch by payload values.

### 2) Spout2 receiver diagnostics: HostCore + WPF visibility

Files:

- `AnimiqCore/host/HostCore/HostController.Automation.cs`
- `AnimiqCore/host/WpfHost/MainWindow.xaml.cs`

- Added `SpoutReceiverDiagnosticsSnapshot` record and `GetSpoutReceiverDiagnostics()` in HostController automation partial.
- Wired native query path through `nc_get_spout_receiver_diagnostics(...)`.
- Updated WPF runtime text generation to include receiver diagnostics line:
  - `rc`
  - `active`
  - `channel`
  - `err`
- Updated automation status text to include receiver active/channel/error and workflow `lastError` field.

Operational result:

- receiver state is now visible in operator-facing diagnostics and automation panel status,
- sender/receiver parity for day-2 diagnosis improved without requiring debugger attachment.

### 3) Host tracking/expression path hardening

File: `AnimiqCore/host/HostCore/HostController.cs`

- Isolation mode flag is set to `false` in current state, while preserving isolation-path logic gates.
- Neutral expression reset is expanded to include:
  - all discovered native expression names
  - ARKit52 canonical keys
  - common VRM/viseme/emotion/look aliases
- Replaced broad VRM preset alias emission with `SyncVrmPresetChannelsSafe(...)`:
  - keep blink aliases synchronized
  - force-clear vowel/emotion/look aliases to prevent unintended outfit/visibility clip activation.
- Tuned upper-body inferred pose mapping parameters (position-derived head mapping, trunk/shoulder/upper-arm gains and clamps) for lower artifact risk and steadier behavior.

Operational result:

- fewer stale-expression leftovers across avatar/profile transitions,
- reduced accidental preset alias activation side effects,
- smoother upper-body drive envelope under noisy input.

### 4) Tracking input normalization resilience

File: `AnimiqCore/host/HostCore/TrackingInputService.cs`

- Expanded alias normalization for blink/jaw variants:
  - accepts additional `eyeblink*` spellings and jaw synonyms.
- Added percentage-to-unit conversion guard for expression weights:
  - if incoming value is likely 0..100 scale (`> 1.5`), convert to 0..1 before post-processing.

Operational result:

- better compatibility with heterogeneous tracker payload conventions,
- less manual remapping work for operator integration.

### 5) Arm pose policy and native fallback behavior

Files:

- `AnimiqCore/src/nativecore/native_core.cpp`
- `AnimiqCore/host/HostCore/HostFeatureGates.cs`

- Arm-pose support gate narrowed to MIQ-only lane (runtime + host feature-gate messaging aligned).
- Added side-agnostic helper fallback for bones without explicit left/right naming hints:
  - applies averaged bilateral upper-arm pose
  - includes helper token families (`upperarm/uparm`, `armtwist/armroll/sleeve`)
- Keeps helper-bone weighted application policy (`1.0` for main upper-arm, `0.35` for twist/sleeve helpers).

Operational result:

- consistent policy surface between native runtime and host gating text,
- improved deformation continuity for rigs with nonstandard arm helper naming.

## Verification

Executed:

- `dotnet build AnimiqCore/host/HostCore/HostCore.csproj -c Release --no-restore`
- `dotnet build AnimiqCore/host/WpfHost/WpfHost.csproj -c Release --no-restore`

Observed:

- HostCore build: PASS (existing unrelated warnings in `HostController.cs` about unreachable code)
- WpfHost build: PASS
- one transient file-lock (`CS2012`) occurred during parallel build attempt and was resolved by sequential rerun.

## Notes

- This report documents only currently pending workspace deltas intended for the requested commit.
- Untracked external folder `vnyan/` is excluded from commit scope.
