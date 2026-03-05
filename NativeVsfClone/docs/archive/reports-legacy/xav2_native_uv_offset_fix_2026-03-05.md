# XAV2 Native UV Offset Fix (2026-03-05)

## Summary

This update fixes a native render-path issue where `.xav2` avatars loaded successfully but displayed severely corrupted texture mapping (noise/scrambled pattern), despite valid texture payload availability.

Observed behavior:

- host runtime status remained healthy (`Render: Ok`, no fatal last error).
- draw calls were produced.
- mesh silhouette and geometry looked broadly correct.
- UV-driven texture sampling appeared broken across large mesh regions.

## Root Cause

In native mesh upload (`BuildGpuMeshForPayload`), UV extraction assumed UV bytes started at offset `12` whenever stride was at least `20`.

That assumption is valid for compact `pos3 + uv2` layouts (20 bytes), but invalid for Unity XAV2 export layout (48 bytes):

- XAV2 vertex layout from Unity exporter:
  - position (`float3`) at offset `0`
  - normal (`float3`) at offset `12`
  - uv (`float2`) at offset `24`
  - tangent (`float4`) at offset `32`

Because native path read UV from offset `12`, it sampled normals as UVs, causing texture-coordinate corruption.

## Changes Implemented

Updated file:

- `src/nativecore/native_core.cpp`

Implementation:

- In `BuildGpuMeshForPayload(...)`, introduced stride-aware UV offset selection:
  - `uv_offset = 24` when `src_stride >= 32` (Unity/XAV2-style expanded vertex payload)
  - `uv_offset = 12` otherwise (legacy compact payload compatibility)
- UV extraction now reads from `base + uv_offset` and validates `src_stride >= uv_offset + 8`.
- Existing zero-UV fallback remains unchanged when payload does not provide UV bytes.

## Compatibility / Behavior Impact

No API/format changes:

- no `xav2` section schema change
- no host API change
- no Unity package public API change

Behavioral impact:

- fixes UV interpretation for expanded-stride XAV2 meshes.
- preserves compatibility for older compact mesh payloads.

## Validation

Build verification executed:

```powershell
dotnet build NativeVsfClone\host\HostCore\HostCore.csproj -c Release
cmake --build NativeVsfClone\build --config Release
```

Outcome:

- HostCore build: PASS
- nativecore build: PASS

Manual render verification recommended:

1. Load previously broken `.xav2` sample in host.
2. Confirm texture mapping matches Unity source view (non-scrambled UV presentation).
3. Confirm no regression on compact/legacy payload assets.

## Files in This Update

- `src/nativecore/native_core.cpp`
- `docs/reports/xav2_native_uv_offset_fix_2026-03-05.md`
- `docs/reports/session_change_summary_2026-03-05.md`
- `docs/INDEX.md`
