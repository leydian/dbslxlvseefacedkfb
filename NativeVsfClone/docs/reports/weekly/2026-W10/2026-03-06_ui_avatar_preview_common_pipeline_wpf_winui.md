# UI Avatar Preview Common Pipeline (WPF + WinUI)

## Scope
- Added pre-load avatar preview parity to both host UIs.
- Refactored thumbnail generation pipeline into `HostCore` for shared behavior.
- Kept worker-process thumbnail rendering model to isolate failures from main UI process.

## Implemented Changes
- Shared thumbnail worker and queue pipeline added under `host/HostCore`.
  - `AvatarThumbnailWorker`: thumbnail worker entry (`--thumbnail-worker`), hash-based thumbnail path, worker process execution helper.
  - `AvatarThumbnailPipeline`: deduplicated queue, pending/ready/failed state transitions, worker-running notifications.
- WPF host migrated to shared pipeline.
  - Removed local thumbnail queue/worker runner logic from `MainWindow.xaml.cs`.
  - Replaced with shared pipeline subscription for status updates.
  - `ThumbnailWorker.Run` now delegates to `HostCore.AvatarThumbnailWorker.Run`.
- WinUI host upgraded to match WPF preview UX.
  - Added preview panel in Avatar section: image preview, status text, retry button, recent avatar list.
  - Wired selection/input/load-success flows to record avatar selection and enqueue thumbnail generation.
  - Added app-level `--thumbnail-worker` command handling in WinUI `App.xaml.cs`.

## Behavior Summary
- When a valid avatar path is selected:
  - Avatar is recorded into recent list.
  - Thumbnail job is queued (unless cached thumbnail already exists and `force=false`).
  - Preview state transitions: `pending -> ready` or `pending -> failed`.
- Retry action always re-queues thumbnail generation with `force=true`.
- Recent avatar list and preview status are refreshed from persisted `RecentAvatars` state.
- Button enable/disable rules now consider shared pipeline worker-running state.

## Verification Summary
- Static verification:
  - Confirmed event wiring and handlers in WPF/WinUI for preview queue and UI refresh.
  - Confirmed WinUI XAML bindings for new preview controls and handlers.
- Build verification:
  - `dotnet build` attempted for `HostCore`, `WpfHost`, `WinUiHost`.
  - Restore/build blocked in current environment by NuGet network access (`NU1301`, `api.nuget.org:443`), so compile-time validation could not be completed here.

## Known Risks or Limitations
- Because restore is blocked in this environment, compile/runtime verification remains pending.
- Existing local workspace includes unrelated modifications; this change set was staged by explicit file list only.

## Next Steps
1. Run `dotnet restore` and `dotnet build` in a network-enabled environment.
2. Execute manual UI smoke checks:
   - WPF: path select, retry, recent list selection, restart persistence.
   - WinUI: same parity checks plus `--thumbnail-worker` launch path.
3. If needed, move shared constants (thumbnail size/timeout/status strings) into a single options contract for stricter consistency.
