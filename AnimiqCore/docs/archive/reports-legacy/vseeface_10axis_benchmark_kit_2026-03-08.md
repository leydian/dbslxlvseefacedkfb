# VSeeFace v1.13.38c2 10-Axis Benchmark Kit

## Change Summary
Implemented a runnable benchmark kit to compare a focus runtime (default: `animiq`) against the reference runtime `vseeface` fixed at version `1.13.38c2`.

Primary deliverables:

- `tools/vseeface_benchmark_manifest.example.json`
- `tools/vseeface_benchmark_observations.template.json`
- `tools/vseeface_benchmark_scorecard.ps1`

Generated verification artifacts:

- `build/reports/vseeface_benchmark_scorecard.json`
- `build/reports/vseeface_benchmark_scorecard.txt`
- `build/reports/vseeface_benchmark_scorecard.md`

## Benchmark Design (10 Axes)
Balanced benchmark axes and intent:

1. `e2e_latency`: motion-to-avatar response speed.
2. `tracking_retention`: tracking continuity under disturbance.
3. `wink_blink_accuracy`: intentional wink/blink precision.
4. `lipsync_quality`: mouth shape and phoneme alignment quality.
5. `gaze_tracking`: gaze response latency and stability.
6. `resource_efficiency`: CPU/GPU/FPS efficiency.
7. `multi_instance_camera_sharing`: same-camera multi-instance behavior.
8. `vmc_reliability`: VMC send/receive drop or miss reliability.
9. `camera_spout_compatibility`: virtual camera and Spout startup/interop reliability.
10. `long_run_stability`: crash/memory growth/drift behavior in prolonged runs.

## Manifest Contract
`tools/vseeface_benchmark_manifest.example.json` defines:

- benchmark metadata: `benchmarkName`, `version`, `baselineProduct`, `baselineVersion`, `focusProduct`.
- execution baseline: `iterationsPerAxisPerProduct`.
- axis contracts: `id`, `name`, `owner`, `weight`.
- KPI contracts per axis: `key`, `better`, `unit`.
- prioritization defaults: `riceDefaults` (`reach`, `impact`, `confidence`, `effort`).

Scoring assumptions encoded in the script:

- KPI direction uses `better` (`lower` or `higher`).
- axis score uses KPI-average of a 1-5 mapped score.
- overall score uses weighted average of axis scores.
- backlog priority uses `RICE * max(0, 3.0 - axis_score)`.

## Observation Schema
`tools/vseeface_benchmark_observations.template.json` uses `records[]`.
Each record is required to include:

- `test_id`
- `scenario`
- `tool_name`
- `tool_version`
- `hardware_profile`
- `quality_preset`
- `axis_id`
- `metric_name`
- `value`
- `unit`
- `sample_count`
- `avg`
- `p95`
- `worst`
- `notes`
- `pass_fail`

Validation behavior:

- missing required fields fail closed.
- numeric aggregation uses `avg` first; falls back to `value`.
- pass rate is aggregated per product and metric via `pass_fail`.

## Scorecard Script Behavior
`tools/vseeface_benchmark_scorecard.ps1` performs:

- path normalization and strict input validation.
- per-axis metric aggregation for `focus` vs `baseline`.
- per-metric outputs: `focus_avg`, `baseline_avg`, `score_1_to_5`, `relative_index_baseline_100`, pass rates, sample counts.
- per-axis outputs: score, gap from neutral, relative index, RICE score.
- global outputs: weighted score and weighted relative index.
- priority backlog sorted by descending RICE for below-neutral axes.

Output files:

- JSON: full machine-readable breakdown.
- TXT: compact operational summary.
- MD: dashboard-friendly summary table.

## Validation Run
Executed command:

```powershell
.\tools\vseeface_benchmark_scorecard.ps1 `
  -ManifestPath .\tools\vseeface_benchmark_manifest.example.json `
  -ObservationsPath .\tools\vseeface_benchmark_observations.template.json
```

Observed result:

- script completed successfully.
- all three report files were generated under `build/reports`.
- sample template data produced a non-empty scorecard and axis table.
