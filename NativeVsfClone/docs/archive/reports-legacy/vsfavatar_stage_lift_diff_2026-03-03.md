# VSFAvatar Fixed-Set Diff Report (2026-03-03)

## Scope

Comparison between pre-pass fixed-set behavior and post-pass behavior after reconstruction stage-lift updates.

## Input Set

- `NewOnYou.vsfavatar`
- `Character vywjd.vsfavatar`
- `PPU (2).vsfavatar`
- `VRM dkdlrh.vsfavatar`

## Before (previous fixed-set baseline)

- Dominant stage: `failed-reconstruction`
- Dominant primary error: `DATA_BLOCK_READ_FAILED`
- Dominant offset family: `aligned-after-metadata`
- Candidate score snapshot: `best_candidate_score=10`
- Read failure tuple available on sampled failures

## After (current run)

Source:

- `build/reports/vsfavatar_probe_latest_after_scoring.txt`

Observed:

- Stage:
  - all fixed samples: `failed-serialized`
- Primary error:
  - remains `DATA_BLOCK_READ_FAILED`
- Offset family:
  - shifted to `after-metadata` for fixed-set winners
- Evidence tuple:
  - `failed_block_read_offset`, `failed_block_compressed_size`, `failed_block_uncompressed_size` present

## Gate Result

- `GateA_NoCrashAndDiagPresent=PASS`
- `GateB_AtLeastOneFailedSerializedOrComplete=PASS`
- `GateC_ReadFailureHasOffsetModeSizeEvidence=PASS`

## Notes

- This pass improves stage progression and diagnostic continuity without claiming mesh/object decode completion.
- Current compatibility for fixed VSFAvatar samples remains `partial`.
