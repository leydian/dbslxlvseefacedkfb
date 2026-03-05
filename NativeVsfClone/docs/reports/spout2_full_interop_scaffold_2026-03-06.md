# Spout2 Full Interop Scaffold Implementation (2026-03-06)

## Summary

Implemented the first integration pass for Spout2 full interop with a safe release policy:

- prefer GPU texture submit path for Spout output
- preserve legacy shared-memory sender as automatic fallback
- keep public C ABI unchanged (`nc_start_spout`, `nc_stop_spout`, `NcSpoutOptions`)
- add build-time Spout2 SDK detection and gate automation

This pass is an infrastructure scaffold + runtime routing update. It does **not** yet include concrete Spout2 SDK send/init/release calls.

## Scope Completed

### 1) Build system and feature flags

- Added `VSFCLONE_ENABLE_SPOUT2` CMake option (`ON` by default).
- Added SDK detection logic at `third_party/Spout2/include`.
- Added conditional compile definition `VSFCLONE_SPOUT2_ENABLED=1` when SDK include path is detected.
- Added library hint lookup for `SpoutLibrary.lib` under common `third_party/Spout2` locations.
- Added explicit fallback message when SDK is missing.

Files:

- `CMakeLists.txt`

### 2) Streaming interface extension (internal)

Extended `IStreamingOutput` with optional GPU submit methods while preserving existing byte-submit path:

- `WantsGpuTextureSubmit()`
- `SubmitFrameTexture(void* d3d11_device, void* d3d11_texture)`
- `ActiveBackendName()`
- `FallbackCount()`

This keeps existing callers valid and enables backend negotiation without C ABI changes.

Files:

- `include/vsfclone/stream/i_streaming_output.h`

### 3) Spout sender backend routing

Refactored `SpoutSender` to support backend mode state:

- `Spout2Gpu` (target path)
- `LegacySharedMemory` (fallback path)

Behavior:

- `Start()` initializes legacy shared-memory sender and marks stream active.
- Runtime attempts GPU submit via `SubmitFrameTexture(...)`.
- On GPU submit failure, sender degrades to legacy backend and increments fallback counter.
- `SubmitFrame(...)` now acts as legacy transport only.
- Added backend/fallback accessors for diagnostics.

Spout2-specific methods are present as integration points:

- `TryInitSpout2(...)`
- `TrySendSpout2(...)`
- `StopSpout2()`

These are currently placeholders pending SDK API binding implementation.

Files:

- `include/vsfclone/stream/spout_sender.h`
- `src/stream/spout_sender.cpp`
- `include/vsfclone/stream/spout_sender_stub.h`
- `src/stream/spout_sender_stub.cpp`

### 4) Native render loop routing (GPU-first)

Updated render path to prefer GPU submit for active Spout:

- acquire render-target resource from RTV
- call `SubmitFrameTexture(device, resource)`
- if not submitted, fallback to existing `CaptureRtvBgra(...)` + `SubmitFrame(...)`

This introduces GPU-first behavior without removing legacy compatibility.

File:

- `src/nativecore/native_core.cpp`

### 5) New automation gate

Added new gate script:

- `tools/spout2_interop_gate.ps1`

Capabilities:

- detects Spout2 SDK include path presence
- reads `VSFCLONE_ENABLE_SPOUT2` from CMake cache
- optional strict mode: `-RequireSpout2Configured` (fails when SDK is absent)
- optional host-e2e chaining
- writes summary report to `build/reports/spout2_interop_gate_summary.txt`

### 6) Gate pipeline wiring

Added optional Spout2 interop execution switches:

- `tools/run_quality_baseline.ps1` -> `-EnableSpout2Interop`
- `tools/release_readiness_gate.ps1` -> `-EnableSpout2Interop`

Artifacts list now includes Spout2 interop summary output.

### 7) README usage update

Updated build and gate command examples to include:

- CMake option `-DVSFCLONE_ENABLE_SPOUT2=ON`
- Spout2 fallback note
- `tools/spout2_interop_gate.ps1 -RequireSpout2Configured`

## Verification Executed

Local verification results in this workspace:

1. Configure:

```powershell
cmake -S NativeVsfClone -B NativeVsfClone\build -G "Visual Studio 17 2022" -A x64 -DVSFCLONE_ENABLE_SPOUT2=ON
```

- PASS
- observed: Spout2 SDK include path missing -> fallback message emitted

2. Build:

```powershell
cmake --build NativeVsfClone\build --config Release --target nativecore
```

- PASS

3. Interop gate (non-strict):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\spout2_interop_gate.ps1 -SkipHostE2E
```

- PASS

4. Interop gate (strict config requirement):

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\spout2_interop_gate.ps1 -SkipHostE2E -RequireSpout2Configured
```

- FAIL as expected in current environment (SDK not present)

## Remaining Work (for true full interop)

1. Implement actual Spout2 SDK API calls in sender integration points.
2. Add runtime diagnostics surfacing of active backend and fallback count in host UI/log export.
3. Add receiver-level compatibility automation for OBS + SpoutDemo path checks.
4. Add performance regression comparison for Spout-on frame-time deltas.

## Changed Files (this pass)

- `CMakeLists.txt`
- `include/vsfclone/stream/i_streaming_output.h`
- `include/vsfclone/stream/spout_sender.h`
- `include/vsfclone/stream/spout_sender_stub.h`
- `src/stream/spout_sender.cpp`
- `src/stream/spout_sender_stub.cpp`
- `src/nativecore/native_core.cpp`
- `tools/spout2_interop_gate.ps1`
- `tools/run_quality_baseline.ps1`
- `tools/release_readiness_gate.ps1`
- `README.md`
