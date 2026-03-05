# build/reports Policy

Generated artifacts in this directory are temporary execution outputs.

## Retention Rules

- Keep one `latest` file per report family.
- Keep only high-signal milestone snapshots needed for comparisons.

## Archive Rules

- Move intermediate or noisy outputs to `docs/archive/build-reports/`.
- Keep long-form analysis in `docs/reports/*.md` and link artifacts from there.

## Example Keep Set

- `vsfavatar_probe_latest_after_scoring.txt` (`latest`)
- `vsfavatar_probe_latest_decode_tuning.txt` (milestone)
- `vsfavatar_probe_latest_block0_hypothesis.txt` (milestone)
- `vsfavatar_probe_sidecar.txt` (milestone)
- `vsfavatar_probe_fixed.txt` (baseline snapshot)
