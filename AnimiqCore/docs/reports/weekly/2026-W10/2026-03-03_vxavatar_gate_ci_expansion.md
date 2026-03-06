# VXAvatar/VXA2 Gate CI Expansion (2026-03-03)

## Summary

Expanded the VXAvatar/VXA2 quality-gate harness from local fixed-set checks to profile-based strict gates (`quick`/`full`) with CI integration and machine-readable summary output.

## Scope

- Updated `tools/vxavatar_sample_report.ps1`
- Updated `tools/vxavatar_quality_gate.ps1`
- Added CI workflow `.github/workflows/vxavatar-gate.yml`

## Profile Modes

- `quick`
  - fixed baseline + synthetic corruption samples
  - intended for fastest deterministic PR checks
- `full`
  - fixed baseline + discovered real samples + synthetic corruption
  - intended for broader regression coverage

## Gate Set

- Gate A: fixed `.vxavatar` success contract
- Gate B: synthetic `.vxavatar` corruption contract
- Gate C: `.vxa2` fixed + corruption contract
- Gate D: required output-field contract
- Gate E: full-profile real-sample contract

All gates are strict-fail (`exit 1`) on violations.

## Outputs

- Probe report:
  - `build/reports/vxavatar_probe_latest.txt`
- Text summary:
  - `build/reports/vxavatar_gate_summary.txt`
- JSON summary:
  - `build/reports/vxavatar_gate_summary.json`

## CI Behavior

Workflow: `.github/workflows/vxavatar-gate.yml`

- `quick-gate` job
  - build + `-UseFixedSet -Profile quick`
- `full-gate` job
  - build + `-Profile full`
- Both jobs upload report artifacts for triage.

## Notes

The harness now supports local deterministic checks and CI enforcement with the same script contracts, reducing drift between developer and PR environments.
