# VSFAvatar GateD 1-Sample Pass via UnityFS LZMA + Reconstruction Tuning (2026-03-03)

## Summary

This pass targeted the fixed-set GateD milestone:

- `>=1 sample` must satisfy `probe_stage=complete`, `object_table_parsed=true`, `primary_error_code=NONE`.

Result:

- GateA: PASS
- GateB: PASS
- GateC: PASS
- GateD: PASS
- Overall: PASS

Three of four fixed samples now reach `complete`.

## Scope Delivered

- UnityFS data reconstruction path updates (`TryReconstructDataStream`, `ReconstructStreamAt`).
- UnityFS `mode=1` LZMA decode support.
- Block0 mode-candidate ordering and scoring reweight.
- Failure code granularity expansion.
- Sidecar/loader additive diagnostics (schema v3 compatible).

## Key Implementation

### 1) LZMA decode path

Files:

- `src/vsf/unityfs_reader.cpp`
- `third_party/LzmaDec.c`
- `third_party/LzmaDec.h`
- `third_party/Types.h`
- `CMakeLists.txt`

Changes:

- Added UnityFS `mode=1` decode via `LzmaDecode(...)`.
- Added variant attempts:
  - `props-only-header`
  - `props+size-header`
- Kept strict output size check (`dest_len == expected_size`).

### 2) Reconstruction candidate and score tuning

File:

- `src/vsf/unityfs_reader.cpp`

Changes:

- Candidate offset windows changed from dense single sweep to coarse+fine windows.
- Block0 mode order updated to avoid over-anchoring on header-derived mode.
- Candidate score now emphasizes:
  - decoded block ratio
  - block continuity
  - node-range pass

### 3) Failure classification and diagnostics

Files:

- `include/vsfclone/vsf/unityfs_reader.h`
- `src/vsf/unityfs_reader.cpp`
- `tools/vsfavatar_sidecar.cpp`
- `src/avatar/vsfavatar_loader.cpp`

Changes:

- Added failure detail split:
  - `DATA_BLOCK_SEEK_FAILED`
  - `DATA_BLOCK_READ_FAILED`
  - `DATA_BLOCK_RANGE_FAILED`
  - `DATA_BLOCK_LZMA_*`
  - `DATA_BLOCK_DECOMPRESS_FAILED`
  - `DATA_BLOCK_MODE_UNSUPPORTED`
- Added additive fields:
  - `lzma_decode_attempted`
  - `lzma_decode_variant`
  - `block0_mode_rank`
  - `recon_failure_detail_code`

## File-Level Detailed Summary

### `src/vsf/unityfs_reader.cpp`

- Added `LzmaDecompressExact(...)`:
  - wraps `LzmaDecode(...)` one-call path
  - tries two stream variants
  - enforces exact destination size
- Extended `DecompressByMode(...)`:
  - `mode=1` no longer unimplemented
  - emits LZMA attempt/variant diagnostics
- Updated `BuildModeCandidates(...)` and `BuildBlockModeCandidates(...)`:
  - includes LZMA candidate path
  - block0 prioritization changed to reduce incorrect early lock-in
- Updated `ClassifyFailureCode(...)`:
  - split `DATA_BLOCK_READ_FAILED` umbrella into seek/read/range/decompress subsets
- Updated `TryReconstructDataStream(...)`:
  - candidate window generation changed to coarse then fine
  - score now favors decoded-block ratio and node-range validation
- Updated raw serialized fallback:
  - tighter stride and larger parse window per candidate
  - better chance to populate serialized candidate/attempt counters

### `include/vsfclone/vsf/unityfs_reader.h`

- Added probe fields (additive only):
  - `block0_mode_rank`
  - `lzma_decode_attempted`
  - `lzma_decode_variant`
  - `recon_failure_detail_code`

### `tools/vsfavatar_sidecar.cpp`

- Added JSON output fields:
  - `block0_mode_rank`
  - `lzma_decode_attempted`
  - `lzma_decode_variant`
  - `recon_failure_detail_code`
- Added warning lines:
  - `W_LZMA`
  - `W_RECON_DETAIL`

### `src/avatar/vsfavatar_loader.cpp`

- Reads new optional sidecar fields and maps them to warnings only.
- Existing parsing contract remains unchanged; no required-key break.

### `CMakeLists.txt` + `third_party/*`

- Enabled C language in project for C decoder source compilation.
- Added public-domain LZMA decoder source and headers.
- Added include path for `third_party`.

## Acceptance Criteria Mapping

- `build/reports/vsfavatar_gate_summary.txt` GateD PASS: satisfied.
- `build/reports/vsfavatar_probe_latest_after_gate.txt` complete sample >= 1: satisfied (3 samples complete).
- sidecar/loader no crash on fixed set: satisfied.
- GateA/B/C regression: no regression; all PASS.

## Verification

## Commands

```powershell
./build.ps1 -Configuration Release
./tools/vsfavatar_quality_gate.ps1 -UseFixedSet
./tools/vrm_quality_gate.ps1
./tools/vxavatar_quality_gate.ps1
```

### Gate output snapshot

Source: `build/reports/vsfavatar_gate_summary.txt`

- NewOnYou.vsfavatar: `complete`, `NONE`
- Character vywjd.vsfavatar: `complete`, `NONE`
- PPU (2).vsfavatar: `complete`, `NONE`
- VRM dkdlrh.vsfavatar: `failed-serialized`

`build/reports/vsfavatar_probe_latest_after_gate.txt` confirms GateD tuple for at least one sample.

### Regression notes

- VRM gate: PASS.
- VX gate quick-profile FAIL is due to missing fixed-valid VX/VXA2 dataset entries (`GateA/GateC` precondition issue), not from VSFAvatar parser behavior changes.

## Remaining Risk

- One fixed sample (`VRM dkdlrh.vsfavatar`) still fails serialized parsing despite successful reconstruction scoring improvements.
- `recon_failure_detail_code` and `lzma_decode_variant` now provide direct triage anchors for next pass.
