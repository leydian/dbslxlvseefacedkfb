# ARKit52 Quality Refinement: Hybrid Fallback + Per-Group Calibration (2026-03-06)

## Summary

Implemented ARKit52 quality-focused refinement to improve real-world expression fidelity while keeping strict mapping as primary behavior.

Primary outcomes:

1. Host now resolves ARKit52 channels with strict-first matching and limited fallback aliases for critical channels.
2. Runtime diagnostics now expose strict/fallback/missing counts plus quality score and quality-stage timing.
3. Tracking calibration now applies per-channel-group profiles (eye/mouth/brow) instead of a single uniform profile.
4. Native expression apply path now supports ARKit fallback alias resolution in ARKit mode and emits fallback warning telemetry.

## Detailed Changes

### 1) Host ARKit52 resolution pipeline

Updated:

- `host/HostCore/Arkit52Channels.cs`
- `host/HostCore/HostController.cs`

Key changes:

- Added fallback candidate table for high-impact channels (`eyeBlink*`, `jawOpen`, `mouthSmile*`, `browInnerUp`, `mouthFunnel`, `mouthPucker`).
- `HostController.Tick()` now uses a dedicated ARKit payload builder:
  - strict channel match first
  - fallback lookup only when strict key is missing
  - per-frame summary metrics calculation
- Added quality score model (`strict + weighted-fallback`) and top-missing key summary.

### 2) Tracking diagnostics contract expansion

Updated:

- `host/HostCore/HostInterfaces.cs`

Added fields to `TrackingDiagnostics`:

- `Arkit52StrictCount`
- `Arkit52FallbackCount`
- `Arkit52TopMissingKeys`
- `Arkit52QualityScore`
- `Arkit52QualityStageMs`

### 3) Tracking input calibration refinement

Updated:

- `host/HostCore/TrackingInputService.cs`

Key changes:

- Replaced uniform adaptive-calibration parameters with channel-group profiles:
  - eye/blink group: faster adaptation + tighter denominator
  - mouth/jaw/tongue group: medium profile
  - brow/nose/cheek group: conservative profile
- Maintains existing calibration warmup/stable state model.

### 4) Native fallback handling and warning telemetry

Updated:

- `src/nativecore/native_core.cpp`

Key changes:

- Added ARKit fallback candidate lookup in `nc_set_expression_weights(...)` ARKit mode.
- When fallback aliases are applied, emits warning code:
  - `W_ARKIT52_FALLBACK_APPLIED`
- Existing ARKit strict bind path and missing-bind warning behavior remains intact:
  - `W_ARKIT52_MISSING_BIND`

### 5) Operator diagnostics visibility (WinUI)

Updated:

- `host/WinUiHost/MainWindow.xaml.cs`

Key changes:

- Tracking status/runtime text now includes:
  - `arkit52_strict`
  - `arkit52_fallback`
  - `arkit52_missing`
  - `arkit52_score`
  - `arkit52_stage_ms`

## Verification

Executed:

```powershell
dotnet build NativeAnimiq/host/HostCore/HostCore.csproj -c Release --no-restore
dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -c Release --no-restore
cmake --build NativeAnimiq/build --config Release --target nativecore
dotnet build NativeAnimiq/host/WinUiHost/WinUiHost.csproj -c Release --no-restore
```

Result:

- `HostCore`: PASS
- `WpfHost`: PASS
- `nativecore`: PASS
- `WinUiHost`: FAIL (environment/network restore blocker: `NU1301`, `api.nuget.org` unreachable)
