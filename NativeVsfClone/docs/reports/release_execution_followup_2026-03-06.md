# Release Execution Follow-up (2026-03-06)

## Summary

Follow-up implementation pass for the 20-item release board focused on previously open automation gaps:

- VSFAvatar GateD trend tracking
- tracking parser fuzz gate
- render performance numeric gate
- avatar load soak gate
- session schema migration regression check
- diagnostics bundle enrichment with repro/environment payloads

## Implemented

1) GateD trend tracking

- added `tools/vsfavatar_gated_trend.ps1`
- parses `vsfavatar_gate_summary*.txt` history and emits:
  - `build/reports/vsfavatar_gate_trend_latest.txt`
  - `build/reports/vsfavatar_gate_trend_latest.json`

2) Tracking parser fuzz gate

- added `tools/tracking_parser_fuzz_gate.ps1`
- runs randomized UDP packet fuzz against `HostCore.TrackingInputService`
- emits:
  - `build/reports/tracking_parser_fuzz_gate_summary.txt`

3) Render performance numeric gate

- added `tools/render_perf_gate.ps1`
- evaluates metrics CSV with numeric thresholds:
  - `p95 frame ms`
  - `p99 frame ms`
  - `drop ratio` (`frame_ms > threshold`)
- emits:
  - `build/reports/render_perf_gate_summary.txt`

4) Avatar load soak gate

- added `tools/avatar_load_soak_gate.ps1`
- repeatedly invokes `avatar_tool.exe` across `.vrm/.xav2/.vsfavatar` samples
- checks per-sample and overall success ratio
- emits:
  - `build/reports/avatar_load_soak_gate_summary.txt`

5) Session migration regression check

- added `tools/session_state_migration_check.ps1`
- added helper console checker:
  - `tools/session_state_migration_check/SessionStateMigrationCheck.csproj`
  - `tools/session_state_migration_check/Program.cs`
- runs checker via `dotnet run` and validates `SessionStateStore.Load()` behavior for:
  - v1 -> v4 migration defaults
  - invalid v4 normalization
  - save/load roundtrip consistency
- emits:
  - `build/reports/session_state_migration_check_summary.txt`

6) Diagnostics bundle enrichment

- updated `host/HostCore/HostController.MvpFeatures.cs`
- `ExportDiagnosticsBundle(...)` now includes:
  - `repro_commands.txt`
  - `environment_snapshot.json`

7) Baseline script integration

- updated `tools/run_quality_baseline.ps1`
- new checks integrated as opt-in flags:
  - `-EnableVsfTrend`
  - `-EnableRenderPerf`
  - `-EnableSoak`
  - `-EnableSessionMigration`
  - `-EnableTrackingFuzz`

## Verification Snapshot

Executed in this follow-up:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_gated_trend.ps1
powershell -ExecutionPolicy Bypass -File .\tools\session_state_migration_check.ps1
powershell -ExecutionPolicy Bypass -File .\tools\tracking_parser_fuzz_gate.ps1
powershell -ExecutionPolicy Bypass -File .\tools\avatar_load_soak_gate.ps1 -SampleDir .. -IterationsPerSample 1 -IncludePatterns "*.xav2"
powershell -ExecutionPolicy Bypass -File .\tools\render_perf_gate.ps1 -MetricsCsvPath .\build\reports\metrics_latest.csv -MinSamples 50
```

Observed:

- `vsfavatar_gated_trend`: PASS (`LatestGateD=PASS`, `LatestOverall=PASS`)
- `session_state_migration_check`: PASS (`v1 migration`, `invalid normalize`, `roundtrip`)
- `tracking_parser_fuzz_gate`: PASS (500 fuzz packets processed, parse/drop counters updated)
- `avatar_load_soak_gate` (quick profile): PASS
- `render_perf_gate` (sample metrics): PASS

## Notes

- new gates are opt-in by default to avoid breaking existing baseline pipelines that do not always have prerequisite artifacts (for example, metrics CSV for render perf gate).
- release board status was updated accordingly in:
  - `docs/reports/release_execution_board_20_2026-03-06.md`
