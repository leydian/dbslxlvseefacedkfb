# VSFAvatar preview/output policy split + render gate metrics expansion (2026-03-06)

## Summary

Implemented a safety-first runtime policy for `.vsfavatar` placeholder payload handling:

- preview path is allowed to render placeholder payloads
- output/runtime render path blocks placeholder-only payloads
- load-failure diagnostics now include explicit parser context fields
- render-gate summary now separates preview pass vs output readiness metrics

This keeps user-visible preview operable while preventing placeholder content from being treated as production-ready render output.

## Changed

### 1) Loader warning-code contract for placeholder payload

- File: `src/avatar/vsfavatar_loader.cpp`
- Added warning code emission:
  - `VSF_PLACEHOLDER_RENDER_PAYLOAD`
- Trigger:
  - sidecar reports `render_payload_mode=placeholder_quad_v1` and provides placeholder mesh payload.
- Purpose:
  - runtime can deterministically identify placeholder-derived payload rows without warning-string parsing.

### 2) Native runtime policy split (preview-only placeholder)

- File: `src/nativecore/native_core.cpp`
- Added policy behavior in `nc_create_render_resources`:
  - if avatar format is `.vsfavatar`
  - and mesh payloads are placeholder-only
  - and `VSF_ALLOW_VSF_PLACEHOLDER_RENDER` is **not** enabled
  - return `NC_ERROR_UNSUPPORTED`
- Added structured error detail fields:
  - `parser_mode`
  - `parser_stage`
  - `primary_error`
  - `mesh_extract_stage`
  - `mesh_payload_count`
- Existing no-mesh error path now also carries:
  - `parser_mode`
  - `mesh_extract_stage`

### 3) Preview worker explicit allow policy

- File: `host/HostCore/AvatarThumbnailWorker.cs`
- Thumbnail worker now sets:
  - `VSF_ALLOW_VSF_PLACEHOLDER_RENDER=1`
  - restores previous value on exit (`finally`)
- Effect:
  - thumbnail/preview process can render placeholder payload for operator visibility
  - main runtime/output remains blocked by default policy

### 4) Gate/report schema and summary split

- File: `tools/vsfavatar_sample_report.ps1`
  - added emitted field:
    - `SidecarRenderPayloadMode`
- File: `tools/vsfavatar_render_gate.ps1`
  - parser reads `SidecarRenderPayloadMode`
  - summary adds:
    - `preview_pass_rows`
    - `output_pass_rows`
    - `placeholder_dependent_rows`
    - `output_readiness`
    - `placeholder_dependency`
    - `target_render_payload_mode`

## Verification

Executed on 2026-03-06:

- `cmake --build NativeAnimiq/build --config Release --target nativecore` -> PASS
- `dotnet build NativeAnimiq/host/WpfHost/WpfHost.csproj -c Release` -> FAIL
  - restore blocked by network policy (`NU1301`, `api.nuget.org:443` access denied in current environment)
- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_render_gate.ps1 -UseFixedSet` -> PASS
  - `Overall: PASS`
  - `preview_pass_rows: 5`
  - `output_pass_rows: 0`
  - `output_readiness: FAIL`
  - `placeholder_dependency: YES`

## Operational Notes

- Current gate signal now clearly indicates:
  - parser/preview path can still pass,
  - while authored output-readiness remains blocked.
- This is intentional for short-term safety and aligns with the policy:
  - "preview visibility first, output correctness strict."
