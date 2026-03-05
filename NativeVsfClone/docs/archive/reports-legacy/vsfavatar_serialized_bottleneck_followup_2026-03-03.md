# VSFAvatar Serialized Bottleneck Follow-up (2026-03-03)

## Summary

This follow-up targeted the remaining serialized parsing bottleneck after the UnityFS LZMA/reconstruction pass.

Outcome:

- Fixed-set 4 samples: all `complete`
- `object_table_parsed=true`: 4/4
- `primary_error_code=NONE`: 4/4
- GateA/B/C/D: PASS

## What Was Bottlenecking

Primary issue was no longer block reconstruction. The blocking path was serialized probing:

- too few candidate windows on some node layouts
- frequent header/window miss in `ParseObjectSummary`
- poor failure observability (single error string, no offset/window detail)

## Implementation

### 1) Candidate expansion and parse window hardening

File:

- `src/vsf/unityfs_reader.cpp`

Changes:

- Expanded node offset deltas to wider ranges when candidate count is low.
- Added additional expansion windows around sparse node candidates.
- Increased minimum serialized parse window to reduce truncated-header misses.

### 2) Structured serialized failure diagnostics

Files:

- `include/vsfclone/vsf/unityfs_reader.h`
- `src/vsf/unityfs_reader.cpp`

Added probe fields:

- `serialized_detail_error_code`
- `serialized_last_failure_offset`
- `serialized_last_failure_window_size`
- `serialized_last_failure_code`

Behavior:

- Best failure detail is captured for node parse and raw scan fallback.
- Success clears detail fields.

### 3) Serialized parse classifier and offset-scan tuning

File:

- `src/vsf/serialized_file_reader.cpp`

Changes:

- Added `SF_METADATA_WINDOW_TRUNCATED` classification.
- Kept fallback offset-scan active but tuned for large buffers:
  - smaller scan limit
  - lower scan hit cap
  - larger scan step
  - bounded sample window

This kept recovery behavior while preventing runtime explosion.

### 4) Sidecar/loader additive diagnostics

Files:

- `tools/vsfavatar_sidecar.cpp`
- `src/avatar/vsfavatar_loader.cpp`

Changes:

- Sidecar JSON emits optional serialized detail fields.
- Added `W_SERIALIZED_DETAIL` warning.
- Loader maps new fields to warnings only (no contract break).

## Verification

## Commands

```powershell
./tools/vsfavatar_sample_report.ps1 -UseFixedSet:$true -SampleDir ../sample -AvatarToolPath ./build/Release/avatar_tool.exe -SidecarPath ./build/Release/vsfavatar_sidecar.exe -OutputPath ./build/reports/vsfavatar_probe_latest_after_gate_new.txt
./tools/vsfavatar_quality_gate.ps1 -UseFixedSet -ReportPath ./build/reports/vsfavatar_probe_latest_after_gate_new.txt -SummaryPath ./build/reports/vsfavatar_gate_summary.txt
./tools/vrm_quality_gate.ps1
```

### Result Snapshot

Source:

- `build/reports/vsfavatar_probe_latest_after_gate_new.txt`
- `build/reports/vsfavatar_gate_summary.txt`

Results:

- NewOnYou.vsfavatar: complete / NONE
- Character vywjd.vsfavatar: complete / NONE
- PPU (2).vsfavatar: complete / NONE
- VRM dkdlrh.vsfavatar: complete / NONE
- GateA/B/C/D: PASS
- Overall: PASS

## Notes

- Parser-track goal is achieved for fixed-set compatibility.
- Run duration remains non-trivial (~6-7 minutes for full fixed-set run), so performance tuning can be a separate follow-up without changing current parser correctness state.
