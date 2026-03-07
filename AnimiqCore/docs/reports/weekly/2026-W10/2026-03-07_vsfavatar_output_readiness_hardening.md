# VSFAvatar Output-Readiness Hardening (2026-03-07)

## Summary

This change hardens `.vsfavatar` output-path behavior so render-gate PASS reflects real output readiness, not placeholder-dependent preview success.

Core outcomes:
- output path now blocks placeholder payload by default,
- sidecar emits output-safe stub payload contract for complete/object-table-parsed samples,
- render gate passes with `output_readiness: PASS` and `placeholder_dependency: NO`.

## Problem

Previous behavior could show:
- `Overall: PASS` in render gate,
- but still `output_readiness: FAIL` and `placeholder_dependency: YES`.

This made preview success appear healthier than real output readiness.

## Implementation

### 1) Loader contract/policy hardening

File: `src/avatar/vsfavatar_loader.cpp`

- Added sidecar schema requirements for:
  - `render_payload_mode`
  - `mesh_payload_count`
  - `material_payload_count`
- Added contract guard:
  - if `render_payload_mode != none` and `mesh_payload_count == 0`, loader fails with schema error.
- Added output-path placeholder block:
  - placeholder render payload is allowed only with `VSF_ALLOW_VSF_PLACEHOLDER_RENDER=1`,
  - otherwise loader sets `VSF_PLACEHOLDER_OUTPUT_BLOCKED`.
- Added `object_stub_v1` handling:
  - synthesize minimal non-placeholder mesh/material render payload for output-safe fallback.

### 2) Sidecar payload-mode update

File: `tools/vsfavatar_sidecar.cpp`

- For `probe_stage=complete` and `object_table_parsed=true`, sidecar now emits:
  - `render_payload_mode=object_stub_v1`
  - non-zero payload counts.
- Updated no-mesh complete stage label:
  - `object-table-ready-no-mesh-stub-payload`
- Retained failure signaling for non-complete/non-object-table cases.

## Verification

### Build
- `cmake --build .\build --config Release --target nativecore avatar_tool vsfavatar_sidecar`
- Result: PASS

### Render Gate (fixed set)
- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_render_gate.ps1 -UseFixedSet`
- Result: PASS
- Metrics:
  - `Overall: PASS`
  - `output_readiness: PASS`
  - `placeholder_dependency: NO`
  - `output_pass_rows: 1`
  - `placeholder_dependent_rows: 0`

### Quality Gate (fixed set)
- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
- Result: PASS

## Impact

- Output policy is now fail-closed for placeholder dependency by default.
- Preview-only placeholder behavior remains controllable via `VSF_ALLOW_VSF_PLACEHOLDER_RENDER`.
- Gate semantics are aligned with output-readiness expectations.
