# Avatar Differential Benchmark Harness (VSeeFace vs Animiq) (2026-03-06)

## Summary

This pass adds a differential benchmark harness that compares Animiq runtime outcomes against VSeeFace observation rows on the same sample IDs.

Added capabilities:

- managed DLL probe for VSeeFace runtime package fingerprinting,
- sample manifest schema for cross-engine benchmark cohorts,
- observation schema for VSeeFace black-box outcomes,
- differential classifier with `P0/P1/P2/NONE` priority output.

## Added Tools

- `tools/vseeface_managed_probe.ps1`
  - scans `VSeeFace_Data/Managed/*.dll`
  - emits hash/version inventory (`json/txt`)
- `tools/avatar_benchmark_manifest.example.json`
  - sample definition (`id/path/sample_class/must_render_visible`)
- `tools/vseeface_observations.example.json`
  - VSeeFace observation rows keyed by `id`
- `tools/avatar_engine_differential_benchmark.ps1`
  - runs `avatar_tool` per manifest sample
  - joins VSeeFace observation rows by sample `id`
  - classifies gaps:
    - `P0`: VSeeFace load OK but Animiq parse/load contract not runtime-ready/NONE
    - `P1`: Animiq loads but quality gap (partial/critical/non-visible)
    - `P2`: warning debt high after successful load
    - `PASS`: no actionable gap
- `tools/vseeface_observation_ingest.ps1`
  - ingests per-sample VSeeFace JSON rows into canonical observation file
  - emits auto-generated `rows[]` keyed by sample `id`

## avatar_tool Contract Extension

Updated `tools/avatar_tool.cpp`:

- `--json-out=<path>` now writes JSON for both success and failure paths.
- Added `renderVisibleHeuristic` boolean to JSON payload.

Failure output contract now remains machine-readable even when process exits non-zero.

## Verification

Executed:

- `cmake --build build_plan_impl --config Release --target avatar_tool` -> PASS
- `avatar_tool does_not_exist.vrm --json-out=...` -> non-zero + JSON file emitted (expected)
- `vseeface_managed_probe.ps1` -> PASS (`build/reports/vseeface_managed_probe.*`)
- `avatar_engine_differential_benchmark.ps1` -> PASS (`build/reports/avatar_differential_benchmark_summary.*`)
- `vseeface_observation_ingest.ps1` -> PASS (`build/reports/vseeface_observations.generated.json`)

Observed differential result on current example set:

- `P0=1, P1=0, P2=0, PASS=2`
- The `P0` row flags VSFAvatar gap where VSeeFace observation is successful but Animiq row remains non-runtime-ready visible state.

## Detailed Change Summary

### 1) Runtime JSON contract hardening (`tools/avatar_tool.cpp`)

- Added failure-path JSON emission when `--json-out` is set so non-zero exits are still machine-consumable.
- Added `renderVisibleHeuristic` output field for coarse render-visibility parity checks.
- Kept existing human-readable stderr/stdout diagnostics unchanged.

### 2) Benchmark corpus + observation schemas

- Added `tools/avatar_benchmark_manifest.example.json`:
  - canonical sample keys: `id`, `path`, `sampleClass`, `mustRenderVisible`.
- Added `tools/vseeface_observations.example.json`:
  - black-box VSeeFace observation keys aligned to the same sample `id`.

### 3) VSeeFace managed runtime fingerprint probe

- Added `tools/vseeface_managed_probe.ps1`:
  - scans `VSeeFace_Data/Managed/*.dll`,
  - records DLL name, file version, product version, and SHA-256 hash,
  - writes both JSON and TXT artifacts for diff-friendly tracking.

### 4) Differential benchmark executor and classifier

- Added `tools/avatar_engine_differential_benchmark.ps1`:
  - resolves manifest paths robustly (absolute, cwd-relative, script-relative),
  - executes `avatar_tool` per sample and collects JSON outputs,
  - joins optional VSeeFace observation rows on sample `id`,
  - applies priority classifier:
    - `P0`: VSeeFace success vs Animiq non-ready/non-visible/fail,
    - `P1`: Animiq success but quality/visibility gap,
    - `P2`: Animiq success with elevated warning debt,
    - `PASS`: no differential action item.

### 5) Verification artifact outputs

- `build/reports/vseeface_managed_probe.json`
- `build/reports/vseeface_managed_probe.txt`
- `build/reports/avatar_differential_benchmark_summary.json`
- `build/reports/avatar_differential_benchmark_summary.txt`
- `build/reports/avatar_differential_error_taxonomy.json`
- `build/reports/avatar_parity_dashboard.md`
- `build/reports/avatar_tool_fail_test.json`

## v1.1 Expansion (Strict Parity)

- Differential summary schema now includes:
  - `gate_profile`, `warning_debt_threshold`,
  - `pass`, `by_extension`, `top_primary_errors`, `top_warning_codes`,
  - `parity_rows[]` with normalized engine rows (`engine/load_ok/runtime_ready/visible/...`).
- Per-row runtime parity fields added:
  - `animiq_runtime_ready`, `animiq_elapsed_ms`,
  - `vseeface_runtime_ready`, `vseeface_elapsed_ms`,
  - `extension`, `must_render_visible`.
- New strict classifier default:
  - VSeeFace `load_ok=true` + Animiq not runtime-equivalent -> `P0`.
  - Post-load quality gap -> `P1`.
  - Warning debt over threshold -> `P2`.
  - Otherwise `PASS`.

## Notes

- This harness is black-box + observation driven; it does not require VSeeFace source code.
- Priority counts are intended to drive immediate backlog ordering.
