# VNyan 10-Axis Benchmark Execution Pack (2026-03-08)

## Summary

Implemented an executable benchmark pack for the approved plan:

- comparison scope fixed to `animiq` vs `vnyan`,
- timebox fixed to `4-week deep dive`,
- 10 benchmark axes with standardized KPI contracts,
- automated scorecard output (`json/txt/md`) and RICE-prioritized backlog.

This update converts the prior high-level benchmark plan into a runnable artifact set with:

- fixed schema contracts for axis/KPI definitions and observation captures,
- repeatable score computation and gap ranking logic,
- weekly documentation integration (`INDEX.md` rebuild + docs gate pass).

## Added Interfaces and Files

- `tools/vnyan_benchmark_manifest.example.json`
  - source-of-truth for 10 benchmark axes, KPI direction (`higher/lower`), axis weights, owner, and RICE defaults.
- `tools/vnyan_benchmark_observations.template.json`
  - canonical data-capture schema (`captures[]`) with per-axis/per-product/per-iteration KPI values.
- `tools/vnyan_benchmark_scorecard.ps1`
  - computes axis-level score (`1-5`) from focus-vs-baseline KPI averages,
  - computes weighted overall score,
  - computes RICE-ranked backlog for axes where `animiq` is below neutral baseline.
  - resolves relative paths against repository root so output location is deterministic regardless of caller working directory.

PowerShell entrypoint:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vnyan_benchmark_scorecard.ps1 `
  -ManifestPath .\tools\vnyan_benchmark_manifest.example.json `
  -ObservationsPath .\tools\vnyan_benchmark_observations.template.json `
  -FocusProduct animiq `
  -BaselineProduct vnyan
```

Generated outputs (default):

- `build/reports/vnyan_benchmark_scorecard.json`
- `build/reports/vnyan_benchmark_scorecard.txt`
- `build/reports/vnyan_benchmark_scorecard.md`

## Detailed Implementation Notes

### 1) Manifest contract (`tools/vnyan_benchmark_manifest.example.json`)

- Added all 10 benchmark axes as canonical IDs:
  - `node_graph_ux`
  - `external_event_integration`
  - `expression_mapping_flexibility`
  - `input_source_scalability`
  - `avatar_swap_workflow`
  - `spout2_2d_interop`
  - `item_system_modularity`
  - `world_preset_operations`
  - `ui_theming_customization`
  - `runtime_operational_options`
- Each axis includes:
  - owner domain (`platform/tracking/avatar/interop/runtime/ui/ops`),
  - weight (used in weighted overall score),
  - KPI definitions with optimization direction (`higher`/`lower`),
  - default RICE inputs (`reach`, `impact`, `confidence`, `effort`).

### 2) Observation contract (`tools/vnyan_benchmark_observations.template.json`)

- Added canonical `captures[]` row format with required columns:
  - `axisId`, `product`, `iteration`, `capturedAt`, `kpis`, `notes`.
- Included sample pair rows (`animiq` and `vnyan`) to validate parser behavior and serve as onboarding example.

### 3) Scorecard engine (`tools/vnyan_benchmark_scorecard.ps1`)

- Added robust validations:
  - manifest/observation path existence checks,
  - schema checks (`axes[]`, `captures[]`).
- Added aggregation behavior:
  - per KPI average by product,
  - per KPI comparative score conversion (`1-5`) using KPI direction,
  - per-axis mean score and gap from neutral (`3.0 - axis_score`),
  - weighted overall score across axes.
- Added prioritization behavior:
  - computes `rice_score` per axis using manifest defaults,
  - ranks only axes with positive gap (`animiq` below neutral relative position),
  - exports priority backlog in all output formats.
- Added deterministic file outputs:
  - JSON (`machine ingestion`),
  - TXT (`quick terminal read`),
  - MD (`report-ready table view`).

## 4-Week Execution Cadence

1. Week 1
- lock hardware/network/session presets and test scene.
- execute axes `1-3` with `3` iterations each for both products.

2. Week 2
- execute axes `4-6` with the same run discipline.
- run mid-review for measurement drift and scenario parity.

3. Week 3
- execute axes `7-10` and re-run low-confidence rows.
- fill observation template with reproducible evidence links.

4. Week 4
- run scorecard script on complete observations.
- sort improvement backlog by `rice_score`.
- finalize 90-day action list mapped to owner domain.

## Acceptance and Validation

- all 10 axes must have:
  - both products present,
  - at least `3` iterations each,
  - numeric KPI values for all declared KPI keys.
- scorecard generation must produce all three outputs (`json/txt/md`).
- backlog must include explicit owner and RICE score for each gap axis.

## Verification Executed

Executed in local workspace:

```powershell
powershell -ExecutionPolicy Bypass -File D:\dbslxlvseefacedkfb\AnimiqCore\tools\vnyan_benchmark_scorecard.ps1 -ManifestPath .\tools\vnyan_benchmark_manifest.example.json -ObservationsPath .\tools\vnyan_benchmark_observations.template.json -FocusProduct animiq -BaselineProduct vnyan
```

Observed:

- generated:
  - `D:\dbslxlvseefacedkfb\AnimiqCore\build\reports\vnyan_benchmark_scorecard.json`
  - `D:\dbslxlvseefacedkfb\AnimiqCore\build\reports\vnyan_benchmark_scorecard.txt`
  - `D:\dbslxlvseefacedkfb\AnimiqCore\build\reports\vnyan_benchmark_scorecard.md`

Executed docs integrity checks:

```powershell
powershell -ExecutionPolicy Bypass -File D:\dbslxlvseefacedkfb\AnimiqCore\tools\docs_rebuild_weekly_indexes.ps1
powershell -ExecutionPolicy Bypass -File D:\dbslxlvseefacedkfb\AnimiqCore\tools\docs_quality_gate.ps1
```

Observed:

- weekly indexes rebuilt successfully for `2026-W10`,
- docs quality gate: `PASS` with no broken links and no UTF-8 issues.

## Notes

- scoring is intentionally relative to baseline product at KPI level to keep early benchmark cycles practical.
- if additional competitors are added later, keep this pack as the strict `animiq-vnyan` lane and add a separate multi-competitor profile.
