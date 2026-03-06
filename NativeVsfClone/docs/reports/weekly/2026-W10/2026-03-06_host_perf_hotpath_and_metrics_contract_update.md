# Host Perf Hotpath + Metrics Contract Update (2026-03-06)

## Summary

This update implements a frame-loop focused optimization and observability hardening pass for HostCore and release tooling.

Primary goals covered:

1. reduce avoidable per-frame allocation/work in HostCore hot paths,
2. make render metrics provenance explicit (live tick vs other sources),
3. expose memory sampling reliability as first-class diagnostics,
4. prevent stale publish artifacts from inflating dist size telemetry.

## Scope

- HostCore runtime path:
  - `host/HostCore/PlatformFeatures.cs`
  - `host/HostCore/HostController.MvpFeatures.cs`
  - `host/HostCore/DiagnosticsModel.cs`
  - `host/HostCore/HostController.cs`
- Release tooling:
  - `tools/render_perf_gate.ps1`
  - `tools/release_gate_dashboard.ps1`
  - `tools/publish_hosts.ps1`

## Detailed Changes

### 1) Frame metric allocation pressure reduction

Updated:

- `FrameMetric` converted from `record class` to `readonly record struct`.
- rolling metrics still use bounded queue semantics (`RollingMetricCapacity`) and now trim with single-step dequeue on overflow.

Impact:

- avoids one heap allocation per frame metric sample.
- reduces GC pressure in long-running render sessions.

### 2) Metrics contract extension (provenance + session + memory sample status)

Updated CSV header from:

- `timestamp_utc,frame_ms,...,auto_quality_step`

to:

- `timestamp_utc,frame_ms,...,auto_quality_step,measurement_source,measurement_session_id,memory_sample_status`

Added runtime fields per sample:

- `measurement_source`: currently `live_tick` for host tick generated metrics.
- `measurement_session_id`: process-scoped metric session id (`Guid`-based).
- `memory_sample_status`: `ok|stale|failed|none`.

Behavior:

- memory sampling remains throttled (500ms window) but each row now reports reliability status.
- status transitions:
  - successful sample -> `ok`
  - skipped due sampling interval with no prior sample -> `stale`
  - sampling exception with previous values available -> `stale`
  - sampling exception with no prior values -> `failed`

### 3) Remove duplicate process-memory collection from diagnostics hot path

Updated diagnostics construction flow:

- `DiagnosticsModel.FromNative(...)` now has an overload that accepts memory override values.
- `HostController.CaptureRuntimeDiagnostics(in NcRuntimeStats)` now passes already-sampled memory values (`_lastWorkingSetMb`, `_lastPrivateMb`) + status.

Impact:

- avoids repeated `Process.GetCurrentProcess()` invocation in a path that can be hit at render cadence.
- keeps runtime diagnostics memory fields aligned with rolling metric sampling policy.

### 4) Adaptive quality window processing guard optimization

Updated:

- cooldown gate check in `ApplyAdaptiveAutoQualityFromWindow()` now runs before building/sorting the window sample array.

Impact:

- skips expensive reverse/take/order/array materialization when adjustment is blocked by cooldown.
- reduces unnecessary CPU churn in normal steady-state ticks.

### 5) Render perf gate observability expansion

Updated `tools/render_perf_gate.ps1`:

- detects new optional columns:
  - `measurement_source`
  - `measurement_session_id`
  - `memory_sample_status`
- adds summary evidence:
  - `LiveTickSamples`
  - `OtherSourceSamples`
  - `UnknownSourceSamples`
  - `MeasurementSessionCount`
  - `MemorySampleOkCount`
  - `MemorySampleStaleCount`
  - `MemorySampleFailedCount`
  - `MemorySampleUnknownCount`

Compatibility:

- if columns are absent in historical CSVs, script reports column presence as `False` and counters as `0`.
- existing gate behavior remains unchanged.

### 6) Release dashboard rows for perf-data trust signals

Updated `tools/release_gate_dashboard.ps1` rows:

- `Render Perf (Live Tick Samples)`
- `Render Perf (Memory Sample Failures)`

Purpose:

- make release decision context explicit for metric provenance and memory-sampling reliability.

### 7) Dist directory pre-clean before publish

Updated `tools/publish_hosts.ps1`:

- added `Clear-DistDirectory` and invoked before WPF/WinUI publish outputs.

Impact:

- prevents stale payload carryover from previous runs.
- improves trust in dist size lines (for example `WPF dist size mb`) by measuring fresh output.

## Verification

Executed:

1. `dotnet build NativeVsfClone/host/HostCore/HostCore.csproj -c Release --no-restore`
   - Result: PASS (`0 warnings`, `0 errors`)
2. `powershell -ExecutionPolicy Bypass -File NativeVsfClone/tools/render_perf_gate.ps1 -MetricsCsvPath NativeVsfClone/build/reports/metrics_latest.csv -MinSamples 50 -MaxP95FrameMs 100 -MaxP99FrameMs 100 -MaxFrameDropRatio 1.0`
   - Result: PASS (script execution/format validation run)
   - New summary fields emitted as expected.
3. `powershell -ExecutionPolicy Bypass -File NativeVsfClone/tools/release_gate_dashboard.ps1 ...`
   - Result: PASS
   - Dashboard includes new rows for live-tick sample count and memory-sample failure count.

## Operational Notes

- Existing historical `metrics_latest.csv` in workspace may not include newly added columns; this is expected until fresh HostCore metric export is produced.
- Current implementation keeps feature behavior unchanged (no forced output/quality policy restrictions) while reducing overhead and improving metric trustability.
