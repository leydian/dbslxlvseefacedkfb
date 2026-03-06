# Memory/Size-First Optimization + Adaptive Quality Implementation (2026-03-06)

## Summary

This update implements the memory/size-first optimization plan with adaptive-quality control and observability expansion.

Primary outcomes:

1. WPF lightweight publish profile now supports reduced distribution footprint through profile-driven publish options.
2. Runtime diagnostics and metrics now include process memory fields (`working_set_mb`, `private_mb`).
3. Render performance gate supports optional memory thresholds.
4. Dashboard surfaces render-memory and WPF dist-size lines for release decisions.
5. Thumbnail cache now has bounded size/entry cleanup to prevent unbounded growth.
6. Avatar unload/shutdown paths trigger best-effort managed memory trim.

## Detailed Changes

### 1) HostCore metrics/contract expansion

Updated:

- `host/HostCore/PlatformFeatures.cs`

Changes:

- `FrameMetric` extended with:
  - `WorkingSetMb`
  - `PrivateMb`
  - `AutoQualityStep`
- `AutoQualityPolicy` extended with adaptive-policy fields:
  - `AutoTuneEnabled`
  - `WindowSampleCount`
  - `DegradeP95FrameMs`
  - `DegradeDropRatio`
  - `RecoverP95FrameMs`
  - `RecoverDropRatio`
- `AutoQualityPolicyStore.Normalize(...)` now clamps and persists the new policy fields.

### 2) Runtime diagnostics memory visibility

Updated:

- `host/HostCore/DiagnosticsModel.cs`

Changes:

- Added runtime memory fields:
  - `WorkingSetMb`
  - `PrivateMb`
- `FromNative(...)` now samples process memory (`Process.GetCurrentProcess()`).
- Existing runtime path/timestamp diagnostics fields remain available; memory fields are appended for operator visibility.

### 3) Host runtime cleanup behavior

Updated:

- `host/HostCore/HostController.cs`

Changes:

- Added `TrimManagedMemory()` (best-effort `GC.Collect` + finalizer wait).
- Called after:
  - successful `UnloadAvatar()`
  - successful `Shutdown()`

Purpose:

- reduce retained managed memory after avatar/session teardown.

### 4) Thumbnail cache bounds

Updated:

- `host/HostCore/AvatarThumbnailWorker.cs`

Changes:

- Added cache caps:
  - `ThumbnailCacheMaxEntries = 256`
  - `ThumbnailCacheMaxBytes = 128MB`
- Added `PruneThumbnailCache(...)` called before creating new thumbnail path.
- Cleanup policy:
  - keeps newest files first
  - evicts oldest while over entry/size caps
  - best-effort; failures do not block thumbnail generation

### 5) Render perf gate memory thresholds

Updated:

- `tools/render_perf_gate.ps1`

Changes:

- Added optional args:
  - `MaxPrivateMb`
  - `MaxWorkingSetMb`
- Added parsing for `private_mb`, `working_set_mb` CSV columns.
- Added summary fields:
  - `AvgPrivateMb`, `P95PrivateMb`
  - `AvgWorkingSetMb`, `P95WorkingSetMb`
- Added gate lines:
  - `GatePrivate`
  - `GateWorkingSet`
- Backward compatibility:
  - if memory columns are absent (or thresholds <= 0), memory gates are effectively disabled.

### 6) Lightweight publish profile + dist-size logging

Updated:

- `tools/publish_hosts.ps1`

Changes:

- Added profile argument:
  - `-Profile default|lightweight`
- Added WPF publish controls:
  - `WpfPublishTrimmed`
  - `WpfPublishReadyToRun`
  - `WpfSelfContained`
- Added profile-resolved publish settings logging.
- Added `Get-DirectorySizeMb` and logs:
  - `WPF dist size mb: ...`
  - `WinUI dist size mb: ...`
- Lightweight profile behavior (current implementation):
  - `self-contained=false`
  - `publishsinglefile=false`
  - `publishtrimmed=false` (WPF trimming unsupported)
  - `publishreadytorun=false`
  - debug symbols disabled for output reduction

### 7) Release dashboard visibility uplift

Updated:

- `tools/release_gate_dashboard.ps1`

Added rows:

- `Render Perf (Overall)`
- `Render Perf (P95 Private MB)`
- `Render Perf (P95 WorkingSet MB)`
- `Host Dist (WPF MB)`

## Verification Snapshot

Executed on 2026-03-06:

1. `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore` -> PASS
2. `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore` -> PASS
3. `tools/publish_hosts.ps1 -SkipNativeBuild -NoRestore -Profile lightweight` -> PASS
4. `tools/release_gate_dashboard.ps1 -ReportDir NativeAnimiq/build/reports ...` -> PASS

Observed artifact values:

- `build/reports/host_publish_latest.txt`
  - `Profile: lightweight`
  - `WPF dist size mb: 267.44`
- `build/reports/release_gate_dashboard.txt`
  - includes new render/memory/dist rows

## Notes

- Existing historical `metrics_latest.csv` may not contain memory columns; in that case:
  - `P95PrivateMb` / `P95WorkingSetMb` can remain `0`
  - memory gates remain disabled unless threshold args are set and memory columns are present
- WPF trimming (`PublishTrimmed=true`) was explicitly avoided due unsupported/unstable path for WPF publish.
