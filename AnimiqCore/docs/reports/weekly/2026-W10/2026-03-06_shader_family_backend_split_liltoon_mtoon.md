# Shader family backend split (liltoon/mtoon) with safe fallback (2026-03-06)

## Summary

Converted runtime rendering from a single shared shader path to a family-dispatched backend path for targeted families.
This round introduces backend-owned pass scheduling for `liltoon` and `mtoon`, keeps non-target families on a stable shared backend, and surfaces backend fallback telemetry through `NcAvatarInfo`.

## What changed

### 1) Backend model and dispatch

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Added runtime backend enum:
  - `common`
  - `liltoon`
  - `mtoon`
- Added backend resolver from material shader family.
- `GpuMaterialResource` now tracks:
  - `backend_requested`
  - `backend_selected`
  - `backend_fallback_applied`
  - `backend_fallback_reason`

### 2) Per-backend pass graph scheduling

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Replaced global draw buckets with backend-scoped queue sets.
- Each backend now runs its own pass sequence:
  1. `DepthOnly`
  2. `ShadowCaster`
  3. `Base` (opaque/mask)
  4. `Outline`
  5. `Emission`
  6. `Blend`
- Pass counters now aggregate per-backend activation and preserve global pass-count summary contracts.

### 3) Pixel shader split and backend-specific bind

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- Expanded renderer resources:
  - `pixel_shader_common`
  - `pixel_shader_liltoon`
  - `pixel_shader_mtoon`
- Runtime draw pass selects and binds pixel shader by backend.
- Added mtoon-variant compile branch through shader macro:
  - `FAMILY_MTOON`
- Shared/common backend remains default for non-target families.

### 4) Safe fallback and warning contracts

Updated:

- `src/nativecore/native_core.cpp`

Changes:

- If backend request cannot be honored (or conservative MIQ path forces simplification), material falls back to `common`.
- Added warning codes:
  - `MIQ_FAMILY_BACKEND_FALLBACK`
  - `VRM_FAMILY_BACKEND_FALLBACK`
- Existing material fallback warning flow is preserved and now includes backend fallback reason in render warning text.

### 5) API + host interop diagnostics

Updated:

- `include/animiq/nativecore/api.h`
- `host/HostCore/NativeCoreInterop.cs`
- `src/nativecore/native_core.cpp`

Changes:

- Added `NcAvatarInfo` fields:
  - `family_backend_fallback_count`
  - `selected_family_backend`
  - `active_passes`
- `FillAvatarInfo` now computes:
  - dominant selected backend for avatar materials
  - backend fallback count
  - active pass set summary (`depth|shadow|base|outline|emission`)
- `last_render_pass_summary` now includes backend and fallback diagnostics context.

## Verification

Executed:

```powershell
cmake --build NativeAnimiq/build --config Release --target nativecore
```

Observed:

- `nativecore` Release build: PASS
- output: `NativeAnimiq/build/Release/nativecore.dll`

Additional host verification attempt:

```powershell
dotnet build NativeAnimiq/host/HostApps.sln -c Release
```

Observed:

- non-zero return in current shell without compile diagnostics
- `msbuild` command not available in current shell PATH

## Follow-ups

- Extend fully separated backend set to `poiyomi` and `standard`.
- Split backend-specific material parse maps (currently backend dispatch is active while material extraction still shares common parse path).
- Add snapshot-based per-backend regression gates (backend-tagged diff thresholds).
