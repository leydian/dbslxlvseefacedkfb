# Release Execution Follow-up (2026-03-06)

## Summary

Follow-up implementation pass for the 20-item release board focused on previously open automation gaps:

- VSFAvatar GateD trend tracking
- tracking parser fuzz gate
- render performance numeric gate
- avatar load soak gate
- session schema migration regression check
- diagnostics bundle enrichment with repro/environment payloads
- NuGet mirror bootstrap and sidecar lock guard
- host end-to-end gate and WinUI minimal repro wrapper
- sample profile split (`fixed_set` / `real_large_set`)

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

8) Remaining automation closure

- added `tools/nuget_mirror_bootstrap.ps1`
- added `tools/sidecar_lock_guard.ps1`
- added `tools/host_e2e_gate.ps1`
- added `tools/winui_xaml_min_repro.ps1`
- added sample profiles:
  - `tools/sample_profiles/fixed_set.txt`
  - `tools/sample_profiles/real_large_set.txt`
  - `tools/sample_profile_resolve.ps1`

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

## Detailed Rollup Addendum (same-day integration update)

This addendum summarizes the integrated implementation set now reflected in `5c335e6` and the post-commit gate-script fix.

### A) XAV2 SDK quality and compression track

- Export options/API expansion:
  - `Xav2ExportOptions` now includes compression controls:
    - `EnableCompression`
    - `CompressionCodec` (`Lz4`)
    - `CompressionLevel` (`Fast`/`Balanced`)
- Export format behavior:
  - default export remains uncompressed v4 for compatibility
  - compression-enabled export writes v5 and uses section flag bit (`0x0001`) for compressed payloads
  - compression is opportunistic and currently focused on large sections (`mesh`, `texture`, `skin`, `blendshape`)
- Runtime loader hardening:
  - added compression decode error mapping (`CompressionDecodeFailed`)
  - v5 compressed sections are decoded via LZ4 envelope (`uncompressed_size + compressed bytes`)
  - unknown section flags still surface diagnostics via existing warning channel
- Native loader parity:
  - native XAV2 loader now accepts v5 and decodes compressed sections with equivalent LZ4 path
  - compressed payload decode failure is surfaced as explicit parse error code/warning
- Test expansion:
  - added exporter compression tests (v4/v5 contract + smaller file expectation + runtime load pass)
  - added runtime compressed section tests (success path + corrupt compressed payload failure path)
  - fixed importer test fixture shader/policy mismatch by aligning sample shader to strict-set default

### B) Format and documentation sync

- `docs/formats/xav2.md` updated to include:
  - version matrix v1/v2/v3/v4/v5
  - section flag definition (`0x0001` compressed payload)
  - compressed payload envelope schema
- package readme updated with compression usage notes and v5 behavior summary.

### C) Release/dashboard visibility

- `tools/release_gate_dashboard.ps1` now reports Unity SDK validation state from:
  - `build/reports/unity_xav2_validation_summary.txt`
  - status contract: `PASS|FAIL|UNKNOWN|NOT_RUN`
- dashboard text/json output now includes `Unity XAV2 Validate` row.

### D) Host E2E gate follow-up fix (post-commit)

- `tools/host_e2e_gate.ps1` now wraps steps in:
  - `Push-Location $repoRoot`
  - `try/finally { Pop-Location }`
- Effect:
  - ensures sidecar/publish/dashboard subprocess paths always execute from repository root
  - avoids path-context drift when gate is launched from external working directories.
