# VXAvatar/VXA2/MIQ Gate H Expansion for MIQ Policy Contract (2026-03-03)

## Summary

This update extends the VXAvatar/VXA2/MIQ gate harness with `Gate H` so native MIQ policy behavior is continuously validated in local gate runs and CI.

The new gate locks contract behavior for:

- `--miq-unknown-section-policy=warn`
- `--miq-unknown-section-policy=ignore`
- `--miq-unknown-section-policy=fail`

using policy-specific fields emitted into the sample report.

## Changed

- `tools/vxavatar_sample_report.ps1`
  - Added policy probe extraction for MIQ rows:
    - `MiqPolicyWarn_PrimaryError`
    - `MiqPolicyWarn_WarningCodes`
    - `MiqPolicyIgnore_PrimaryError`
    - `MiqPolicyIgnore_WarningCodes`
    - `MiqPolicyFail_PrimaryError`
    - `MiqPolicyFail_WarningCodes`

- `tools/vxavatar_quality_gate.ps1`
  - Added `Gate H` (`MIQ unknown-section policy contract`).
  - Added strict field presence checks for policy probe fields.
  - Added policy contract checks:
    - `warn`/`ignore` primary error must be `NONE`.
    - `ignore` warning-code count must be `<= warn`.
    - `fail` primary error must be `NONE|MIQ_UNKNOWN_SECTION_NOT_ALLOWED`.
  - Added `gate_h` output to summary text and JSON.
  - Updated overall pass condition to include Gate H.

- `.github/workflows/vxavatar-gate.yml`
  - Expanded trigger paths to include:
    - `tools/avatar_tool.cpp`
    - `CMakeLists.txt`
  - Updated gate step names to explicit `(A-H)`.

- `README.md`
  - Added Gate H rule description under VXAvatar/VXA2/MIQ quality gate.

## Verification

- Intended validation command:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick
```

- Expected output:
  - `build/reports/vxavatar_probe_latest.txt` contains `MiqPolicy*` fields.
  - `build/reports/vxavatar_gate_summary.txt` includes `GateH`.
  - `build/reports/vxavatar_gate_summary.json` includes `gates.gate_h`.
