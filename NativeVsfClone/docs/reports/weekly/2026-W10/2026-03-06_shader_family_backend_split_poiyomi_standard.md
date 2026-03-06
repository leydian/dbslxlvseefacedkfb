# Shader family backend split extension (poiyomi/standard) with safe fallback (2026-03-06)

## Summary

Extended the family-backend split architecture from `liltoon/mtoon` to include `poiyomi/standard`.
This keeps the same backend-owned pass graph model (`DepthOnly -> ShadowCaster -> Base -> Outline -> Emission -> Blend`) and preserves safety-first fallback behavior to `common`.

## What changed

### 1) Backend model expansion

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Extended `RenderFamilyBackendKind`:
  - `Poiyomi`
  - `Standard`
- Extended backend resolver:
  - `shader_family=poiyomi` -> `Poiyomi`
  - `shader_family=standard` -> `Standard`
- `GpuMaterialResource` backend selection guard now accepts:
  - `liltoon/mtoon/poiyomi/standard`
  - other families remain `common`.

### 2) Renderer pixel shader pipeline extension

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Added renderer PS slots:
  - `pixel_shader_poiyomi`
  - `pixel_shader_standard`
- Pipeline init/ready/release paths now include the new PS resources.
- Shader compile/create path now compiles and creates:
  - `common`
  - `liltoon`
  - `mtoon`
  - `poiyomi`
  - `standard`

### 3) Per-backend pass scheduling extension

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Backend queue container expanded:
  - `std::array<FamilyDrawQueues, 5>`
- Backend index mapping expanded for `Poiyomi` and `Standard`.
- Runtime execution order expanded:
  1. `Common`
  2. `Liltoon`
  3. `Mtoon`
  4. `Poiyomi`
  5. `Standard`
- `draw_pass` now selects PS for:
  - `poiyomi` backend
  - `standard` backend

### 4) Diagnostics behavior

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Existing backend diagnostics contract remains unchanged and now naturally reports expanded family set:
  - `selected_family_backend`
  - `family_backend_fallback_count`
  - `active_passes`
- Safe fallback semantics preserved:
  - incomplete or forced-conservative paths still fall back to `common`.

## Verification

Executed:

```powershell
cmake --build NativeVsfClone/build --config Release --target nativecore
powershell -ExecutionPolicy Bypass -File NativeVsfClone/tools/xav2_render_regression_gate.ps1 `
  -SampleDir . `
  -AvatarToolPath NativeVsfClone/build/Release/avatar_tool.exe `
  -SummaryPath NativeVsfClone/build/reports/xav2_render_regression_gate_summary.txt `
  -JsonSummaryPath NativeVsfClone/build/reports/xav2_render_regression_gate_summary.json `
  -FailOnRenderWarnings `
  -MinSampleCount 1
```

Observed:

- `nativecore` Release build: PASS
- `xav2_render_regression_gate`: PASS
  - gate overall: PASS
  - parser/runtime readiness: PASS
  - primary error NONE: PASS
  - strict critical render warning gate: PASS

## Notes

- This slice focused on backend dispatch/runtime topology expansion.
- Backend-specific material parse-map full separation is still a follow-up.
