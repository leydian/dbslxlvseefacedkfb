# Tracking iFacial Key-Alias Expansion for IFM Left/Right and Prefix Variants (2026-03-06)

## Summary

Implemented HostCore-side key normalization hardening for iFacial IFM ingestion to resolve the runtime symptom where tracking packets were received and parsed successfully but expression coverage remained `arkit52=0/52`.

Primary outcomes:

1. Prefixed key forms (for example `blendShape.*`, `face/*`) can now normalize into canonical ARKit52 channel keys.
2. Compact left/right suffix variants (for example `..._L`, `..._R` after normalization) are expanded into `...left` / `...right` for covered ARKit channel families.
3. Strict allowlist behavior is preserved so only canonical ARKit52 channels (and explicit pose channels) are accepted.

## Problem

Observed runtime state from field diagnostics:

- `tracking=on`
- `format=ifm-v1`
- `packets` increasing
- `parse_err=0`
- `arkit52=0/52`, `missing=52`

This indicated transport and parser health, but effective expression mapping failure due to key-shape mismatch.

## Implementation Details

### 1) Prefix stripping before key alias routing

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- Added `StripIfmKnownPrefixTokens(...)`.
- In `TryNormalizeIfmKey(...)`, normalized raw keys now pass through prefix stripping prior to alias switch mapping.
- Supported leading prefix tokens:
  - `blendshapes`
  - `blendshape`
  - `facial`
  - `face`
  - `bs`

Effect:

- Keys such as `blendShape.eyeBlinkLeft` and `face/eyeBlinkLeft` now collapse into canonical-compatible channel names after normalization.

### 2) Compact left/right alias expansion

File:

- `host/HostCore/TrackingInputService.cs`

Changes:

- Added `TryExpandIfmLeftRightAlias(...)`.
- Added static stem allowlist `IfmLeftRightAliasStems`.
- After base alias switch mapping, compact `l/r` suffix forms are expanded into `left/right` when stem is in allowlist.

Covered stems:

- `eyeblink`, `eyelookin`, `eyelookout`, `eyelookup`, `eyelookdown`, `eyesquint`, `eyewide`
- `browdown`, `cheeksquint`
- `mouthsmile`, `mouthfrown`, `mouthdimple`, `mouthstretch`, `mouthpress`, `mouthlowerdown`, `mouthupperup`
- `nosesneer`

Effect:

- Examples such as `eyeBlink_L`, `mouthSmile_R`, `browDown_L` (post-normalization) map into canonical ARKit52 left/right channels.

### 3) Safety/compat behavior preserved

File:

- `host/HostCore/TrackingInputService.cs`

Behavior unchanged:

- Only keys resolving into:
  - `Arkit52Channels.NormalizedSet`, or
  - pose channels (`headyaw/headpitch/headroll/headpos*`, upper-body pose channels)
  are accepted.
- Unknown keys are still dropped.

## Validation

Executed:

```powershell
dotnet build NativeVsfClone/host/HostCore/HostCore.csproj -c Release --no-restore
```

Result:

- PASS (`0 warnings`, `0 errors`)

## Expected Runtime Impact

- iFacial sessions previously stuck at `arkit52=0/52` under key-shape mismatch should show non-zero ARKit52 submission counts when expression packets are present.
- Parse stability remains unchanged (`parse_err` behavior unaffected by this alias-only expansion).
