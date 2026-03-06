# VRM to MIQ Strict Allowlist and Quality Gate Upgrade (2026-03-06)

## Scope

This report covers the second hardening pass on `tools/vrm_to_miq.cpp`, focused on strict policy control, diagnostics depth, and dedicated quality-gate automation.

In scope:

- Strict-mode policy refinement (`allowlist` support)
- Diagnostics/performance metrics contract expansion
- Compression decision refinement for texture payloads
- New `vrm_to_miq`-specific quality gate script

Out of scope:

- Native runtime solver behavior changes
- Unity importer/exporter contract updates
- CI pipeline wiring beyond local gate script addition

## Implemented Changes

### 1) Strict policy refinement

Added strict allowlist support:

- `--strict-allowlist <path>`

Behavior:

- In `--strict` mode, source warning codes and exporter validation issue codes are normalized and checked against allowlist.
- Codes in allowlist are accepted.
- Codes not in allowlist are rejected and trigger strict failure.
- Strict decision summary is emitted to console and diagnostics payload.

### 2) Diagnostics and perf contract expansion

Added:

- `--perf-metrics-json <path>`

Expanded `--diag-json` with:

- strict policy decision block (`accepted/rejected` lists)
- timing block (`load/validate/write/total`)
- section-level metrics:
  - count per section type
  - raw bytes vs written bytes
- aggregate transport metrics:
  - `rawTotalBytes`
  - `writtenTotalBytes`
  - `maxPayloadBufferBytes`

### 3) Compression heuristics refinement

- Added compression pre-check for texture sections to skip recompression on likely already-compressed blobs:
  - PNG / JPG / DDS / KTX signatures
- Retained v5 section compression envelope compatibility and opportunistic apply policy.

### 4) Dedicated quality gate addition

Added script:

- `tools/vrm_to_miq_quality_gate.ps1`

Gate coverage:

- GateA: conversion + `avatar_tool` load pass
- GateB: diagnostics contract presence/shape
- GateC: strict + allowlist smoke
- GateD: KPI measurement path (optionally enforced)

Artifacts:

- `build/reports/vrm_to_miq_quality_gate_summary.json`
- `build/reports/vrm_to_miq_quality_gate_summary.txt`

## Verification Summary

- Build:
  - `cmake --build NativeAnimiq/build --config Release --target vrm_to_miq`
  - Result: PASS
- Conversion with new options:
  - `vrm_to_miq --diag-json ... --perf-metrics-json ... <sample.vrm> <out.miq>`
  - Result: PASS
- Strict allowlist smoke:
  - `vrm_to_miq --strict --strict-allowlist <allowlist> ...`
  - Result: PASS
- Quality gate run:
  - `tools/vrm_to_miq_quality_gate.ps1`
  - Result: PASS (non-enforced KPI mode)

Observed sample metrics:

- Size reduction: generally positive and high on tested samples
- Write-time reduction: currently negative vs raw (`--no-compress`) baseline on sampled set
- Buffer reduction: currently near zero in sampled set

## Known Risks or Limitations

- Aggressive write-time KPI target is not met in measured samples when compared against no-compress baseline; enforce mode may fail until compression speed path is optimized further.
- Buffer-reduction KPI does not yet materially improve because payload assembly path still reuses a large in-memory section buffer.
- Allowlist format is intentionally simple (line/csv tokens); no wildcard/range/prefix policy yet.

## Next Steps

1. Split large section write path into chunked streaming mode to reduce peak payload buffer memory.
2. Add section-type-specific compression thresholds tuned by measured gain/time tradeoff.
3. Add strict policy profile presets (e.g. `ci`, `release`) on top of allowlist baseline.
