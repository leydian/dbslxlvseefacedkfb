# XAV2 outlier draw policy default change (`autofit_only`) + runtime DLL mismatch recovery (2026-03-06)

## Summary

Resolved the user-facing regression where shoes/accessory meshes disappeared after `.xav2` import by separating camera AutoFit outlier filtering from draw exclusion policy.

Final behavior:

- default runtime policy now keeps outlier meshes in draw passes (`autofit_only`)
- legacy draw-skip behavior is preserved behind an explicit environment switch
- confirmed issue persistence was initially caused by stale runtime DLL in `dist/wpf`

## Root cause

Two conditions overlapped:

1. XAV2 preview outlier filtering was used for both:
   - AutoFit framing bounds
   - draw-phase mesh exclusion
2. Runtime process (`dist/wpf/WpfHost.exe`) was still loading an older `nativecore.dll` that did not contain the policy split change.

Result:

- small detached meshes (shoes/accessories) could be classified as outliers and skipped during rendering.

## Implemented changes

Primary code path:

- `src/nativecore/native_core.cpp`

### 1) Outlier draw policy switch

- Added `VSFCLONE_XAV2_OUTLIER_DRAW_POLICY` resolution logic.
- Supported values:
  - `autofit_only` (default)
  - `skip_draw` (legacy behavior)

### 2) Draw exclusion guard refactor

- Draw-phase outlier skip branches are now gated by policy:
  - when `autofit_only`, outlier flags are still used for camera framing stability, but meshes are not skipped from draw queues.
  - when `skip_draw`, previous draw exclusion logic remains active.

### 3) Format/runtime docs update

- Updated `docs/formats/xav2.md` to document:
  - new env variable
  - default behavior (`autofit_only`)
  - legacy override (`skip_draw`)

## Operational recovery performed

- Built latest runtime module:
  - `cmake --build NativeVsfClone/build --config Release --target nativecore`
- Detected runtime mismatch:
  - `build/Release/nativecore.dll` hash differed from `dist/wpf/nativecore.dll`.
- Replaced stale runtime binary after stopping `WpfHost.exe`.
- Verified `build` and `dist/wpf` hashes matched afterward.

## Verification notes

- Build: PASS (`nativecore`)
- Runtime symptom check (operator): shoes reappeared after DLL sync and relaunch.
- Env check:
  - `VSFCLONE_XAV2_OUTLIER_DRAW_POLICY` unset -> default `autofit_only` path active.

## Recommended operations guidance

To avoid false-negative runtime verification:

1. stop host process before DLL replacement
2. verify hash parity between:
   - `build/Release/nativecore.dll`
   - runtime deployment path (`dist/wpf/nativecore.dll`)
3. relaunch host and retest target `.xav2` samples
