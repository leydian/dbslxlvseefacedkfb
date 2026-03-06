# XAV2 static skinning regression and safe default-off recovery (2026-03-06)

## Summary

This update documents a two-stage recovery for XAV2 render breakage where avatar load remained healthy but preview output intermittently collapsed into a cylindrical/tube-like shape.

Final outcome:

- Root cause confirmed in XAV2 static skinning path under mixed asset quality.
- Runtime stabilized by restoring XAV2 static skinning default to OFF (safety-first), while keeping explicit operator opt-in support.
- Backend fallback regression from earlier renderer split was reduced (`SelectedFamilyBackend=liltoon`, fallback count dropped to zero in validated run).

## Incident timeline and evidence

Observed runtime snapshots:

- `2026-03-06T00:46:48Z`: render looked broken while load contracts were healthy (`RenderRc=Ok`, `PrimaryError=NONE`, `Compat=full`).
- `2026-03-06T00:52:17Z`: diagnostics after backend/material path fixes:
  - `FamilyBackendFallbackCount: 9`
  - `SelectedFamilyBackend: common`
  - `WarningCode: XAV2_BOUNDS_OUTLIER_EXCLUDED`
- `2026-03-06T00:58:15Z`: after deeper hardening:
  - `FamilyBackendFallbackCount: 0`
  - `SelectedFamilyBackend: liltoon`
  - still visually unstable for this sample per operator feedback.
- Final confirmation after safe default rollback:
  - operator-reported tube/cylinder artifact disappeared.

Conclusion:

- Material/backend fallback recovery improved correctness but did not fully eliminate the tube artifact.
- The final blocking issue was static skinning application itself for this avatar class.

## Root cause

XAV2 static skinning auto-application remained vulnerable to low-quality or convention-mismatched skeleton data.

Even with extent explosion guards, some poses could pass finite checks yet still collapse geometry into a narrow shape.

## Implemented changes

Primary file:

- `src/nativecore/native_core.cpp`

### 1) Material/backend safety hardening

- Added env-gated conservative material path for XAV2:
  - `VSFCLONE_XAV2_CONSERVATIVE_MATERIAL`
  - default is OFF, opt-in only.
- Removed unconditional XAV2 conservative material force, so typed material/backend routing can remain active under normal conditions.

### 2) Static skinning collapse guard

- Added collapse detection in addition to explosion detection:
  - collapse criterion based on post-skinning extent ratio vs pre-skinning extent.
- Added warning contract:
  - `XAV2_SKINNING_COLLAPSE_GUARD`
- On guard hit:
  - reject posed vertices
  - fallback to bind pose vertex blob

### 3) Bounds outlier filtering relaxation

- Increased cluster-distance threshold used for XAV2 bounds exclusion.
- Added minimum sample gate before cluster exclusion (`>= 6` samples).
- Goal: reduce over-aggressive autofit exclusions from sparse/noisy mesh sets.

### 4) Final stabilization policy (decisive fix)

- Restored XAV2 static skinning default behavior to OFF in auto mode:
  - XAV2 now requires explicit opt-in for static skinning.
- Existing explicit override still supported:
  - `VSFCLONE_XAV2_ENABLE_STATIC_SKINNING=1` to force ON.

## Verification

Executed:

```powershell
cmake --build NativeVsfClone/build --config Release --target nativecore
dotnet build NativeVsfClone/host/HostCore/HostCore.csproj -c Release --no-restore
dotnet build NativeVsfClone/host/WpfHost/WpfHost.csproj -c Release --no-restore
Copy-Item NativeVsfClone/build/Release/nativecore.dll NativeVsfClone/dist/wpf/nativecore.dll -Force
```

Integrity checks:

- build/dist nativecore hashes matched
- build/dist nativecore timestamps matched
- runtime diagnostics:
  - `RuntimePathMatch: True`
  - `RuntimeModuleStaleVsBuildOutput: False`
  - `RuntimeTimestampWarningCode: none`

Operator result:

- cylindrical/tube artifact no longer visible after XAV2 default-off static skinning policy.

## Operational guidance

- Default recommendation:
  - keep XAV2 static skinning OFF for safety.
- Controlled experimentation only:
  - enable with `VSFCLONE_XAV2_ENABLE_STATIC_SKINNING=1` on known-good avatars.
- If needed for isolated troubleshooting:
  - force conservative material path with `VSFCLONE_XAV2_CONSERVATIVE_MATERIAL=1`.
