# 2026-03-08 VSFAvatar Structured Mesh Default Enable and Blob Scan Uplift

## Background
- VRM runtime recovery was stabilized, and focus moved back to `.vsfavatar` support quality.
- Runtime diagnostics and `avatar_tool` output still showed `VSF_OBJECT_STUB_RENDER_PAYLOAD` for complete-sidecar samples, which meant fallback object stubs were used instead of extracted authored mesh payloads.

## Problem
- `vsfavatar_loader` mesh extraction gates were effectively disabled in common environments and scanned too few mesh blobs.
- As a result, structured/heuristic extraction was skipped or starved, then forced into object-stub fallback.

## Changes
- Updated `src/avatar/vsfavatar_loader.cpp`:
- Changed extraction toggles to default-on with explicit opt-out flags:
  - `enable_structured_mesh = !EnvFlagEnabled("VSF_DISABLE_STRUCTURED_MESH")`
  - `enable_heuristic_mesh = !EnvFlagEnabled("VSF_DISABLE_HEURISTIC_MESH")`
- Increased serialized mesh blob scan limit from fixed `6` to dynamic limit:
  - `max(6, min(96, sidecar_mesh_payload_count * 2))` (fallback base `24` when unknown)
- Relaxed extraction quality thresholds slightly:
  - structured indices threshold: `240 -> 120`
  - heuristic indices threshold: `100 -> 90`

## Deployment
- Redeployed runtime using:
- `tools/publish_hosts.ps1`
- Verified native/wpf publish pipeline completed and smoke probe passed.

## Verification
- Rebuilt diagnostic binary:
- `cmake --build build --config Release --target avatar_tool`
- Ran sample checks:
- `build\\Release\\avatar_tool.exe ..\\sample\\NewOnYou.vsfavatar --dump-warnings-limit=120`
  - `WarningCode[0]: VSF_SERIALIZED_STRUCTURED_MESH_PAYLOAD`
  - `MeshPayloads: 2`
  - `W_STRUCTURED_MESH: payloads=2, indices=1254396`
  - `W_STRUCTURED_TRY: payloads=2, indices=1254396, enabled=true`
- `build\\Release\\avatar_tool.exe "..\\sample\\Character vywjd.vsfavatar"`
  - `WarningCode[0]: VSF_SERIALIZED_STRUCTURED_MESH_PAYLOAD`
  - `MeshPayloads: 2`

## Result
- `.vsfavatar` complete-stage samples are no longer locked to object-stub payloads in default runtime configuration.
- Structured mesh payload extraction is now active by default and validated on representative samples.

## Notes
- Sidecar warning text may still include legacy "object stub payload emitted" wording because sidecar metadata is appended; runtime warning code now reflects structured payload usage when extraction succeeds.
