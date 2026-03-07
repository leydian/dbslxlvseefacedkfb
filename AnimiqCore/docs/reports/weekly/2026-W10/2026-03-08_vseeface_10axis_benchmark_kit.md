# 2026-03-08 VSeeFace 10-Axis Benchmark Kit

## Summary
Added a runnable benchmark kit for a balanced 10-axis comparison against `VSeeFace v1.13.38c2`.

## Added Files
- `tools/vseeface_benchmark_manifest.example.json`
- `tools/vseeface_benchmark_observations.template.json`
- `tools/vseeface_benchmark_scorecard.ps1`
- `docs/reports/vseeface_10axis_benchmark_kit_2026-03-08.md`

## Benchmark Axes
1. `e2e_latency`
2. `tracking_retention`
3. `wink_blink_accuracy`
4. `lipsync_quality`
5. `gaze_tracking`
6. `resource_efficiency`
7. `multi_instance_camera_sharing`
8. `vmc_reliability`
9. `camera_spout_compatibility`
10. `long_run_stability`

## Output and Scoring
- Produces JSON/TXT/MD scorecards under `build/reports`.
- Includes weighted 1-5 score, baseline-normalized relative index (`baseline=100`), and RICE-prioritized backlog.
- Validates fixed observation schema and fails on missing required fields.

## Verification
Ran `tools/vseeface_benchmark_scorecard.ps1` with the provided manifest and observation template and confirmed scorecard outputs were generated.
