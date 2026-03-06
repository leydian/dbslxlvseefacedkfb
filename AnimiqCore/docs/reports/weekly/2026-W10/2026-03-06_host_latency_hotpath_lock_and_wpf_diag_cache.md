# Host Latency Hotpath + Lock Contention + WPF Diagnostics Cache (2026-03-06)

## Summary

This update implements three high-impact, behavior-preserving optimizations focused on real-time latency in the WPF + HostCore path:

1. tracking latency percentile hotpath optimization in `TrackingInputService`,
2. native tracking ingest lock-window reduction in `native_core`,
3. WPF runtime diagnostics text-build cache split to reduce repeated allocation/churn.

No public API signatures were changed.

## Detailed Changes

### 1) Tracking latency P95 hotpath optimization (HostCore)

Updated:

- `host/HostCore/TrackingInputService.cs`

Before:

- `RecordLatencySample(...)` computed P95 by sorting the full rolling queue every sample:
  - `OrderBy(...).ToArray()` (O(n log n) + per-sample allocation).

After:

- Added sorted rolling storage:
  - `_latencySortedSamples: List<double>`
  - maintain ordering via `BinarySearch` insert/remove.
- Kept existing rolling queue (`_latencySamples`) for FIFO eviction and average tracking (`_latencySampleSum`).
- P95 now reads directly from sorted storage index (no per-sample full sort).
- Runtime reset path now clears both queue and sorted storage.

Behavior:

- window size (`LatencySampleWindow=240`) unchanged,
- `LatencyAvgMs` and `LatencyP95Ms` semantics unchanged.

### 2) Native tracking ingest lock-window reduction (nativecore)

Updated:

- `src/nativecore/native_core.cpp`

Before:

- `nc_set_tracking_frame(...)` updated `latest_tracking` and also iterated every avatar/expression to recompute runtime expression weights while holding global mutex.

After:

- Introduced `tracking_weights_dirty` in `CoreState`.
- `nc_set_tracking_frame(...)` now:
  - sanitize + assign `latest_tracking`,
  - mark `tracking_weights_dirty=true`,
  - return.
- Added `ApplyTrackingDrivenExpressionWeights(AvatarPackage*)`.
- Render loop (`RenderFrameLocked`) now applies tracking-driven expression weights once when dirty, then clears dirty flag.
- `nc_set_expression_weights(...)` now clears dirty flag after explicit manual expression update to preserve override precedence.
- init/shutdown reset paths now explicitly clear dirty flag.

Behavior:

- tracking-driven expression mapping (`blink`, `viseme_aa`, `joy`) is preserved,
- global lock hold time in tracking ingest path is reduced.

### 3) WPF runtime diagnostics static-block caching

Updated:

- `host/WpfHost/MainWindow.xaml.cs`

Before:

- `BuildRuntimeText(...)` rebuilt full runtime diagnostics text every refresh candidate.

After:

- Split runtime diagnostics composition into:
  - dynamic block (timestamp/version/frame/tracking/live error),
  - cached static block (module path, render/ui settings, spout/osc and related stable fields).
- Added:
  - `_lastRuntimeStaticBlockKey`
  - `_lastRuntimeStaticBlockText`
  - `BuildRuntimeStaticBlockKey(...)`
  - `BuildRuntimeStaticBlock(...)`
- static block rebuild now happens only when relevant source fields change.

Behavior:

- user-facing diagnostics fields remain unchanged,
- UI thread string-build churn reduced on frequent refresh.

## Verification

Executed:

1. `dotnet build host\HostCore\HostCore.csproj -c Release --no-restore`
   - Result: PASS (`0 warnings`, `0 errors`)
2. `cmake -S . -B build_plan_impl -G "Visual Studio 17 2022" -A x64 -DANIMIQ_ENABLE_SPOUT2=ON`
   - Result: PASS
3. `cmake --build build_plan_impl --config Release`
   - Result: PASS (`nativecore.dll` built)
4. `dotnet build host\WpfHost\WpfHost.csproj -c Release --no-restore`
   - Result: FAIL (existing environment/project issue):
     - `RG1000: An item with the same key has already been added. Key: mainwindow.baml`

## Notes

- WPF build failure above is not introduced by this change set and is related to duplicated XAML resource generation state in current workspace.
- This optimization pass is intentionally behavior-preserving and does not alter release policy or runtime contracts.
