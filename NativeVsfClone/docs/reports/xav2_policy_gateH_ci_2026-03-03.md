# VXAvatar/VXA2/XAV2 Gate H Expansion for XAV2 Policy Contract (2026-03-03)

## Summary

This update extends the VXAvatar/VXA2/XAV2 gate harness with `Gate H` so native XAV2 policy behavior is continuously validated in local gate runs and CI.

The new gate locks contract behavior for:

- `--xav2-unknown-section-policy=warn`
- `--xav2-unknown-section-policy=ignore`
- `--xav2-unknown-section-policy=fail`

using policy-specific fields emitted into the sample report.

## Changed

- `tools/vxavatar_sample_report.ps1`
  - Added policy probe extraction for XAV2 rows:
    - `Xav2PolicyWarn_PrimaryError`
    - `Xav2PolicyWarn_WarningCodes`
    - `Xav2PolicyIgnore_PrimaryError`
    - `Xav2PolicyIgnore_WarningCodes`
    - `Xav2PolicyFail_PrimaryError`
    - `Xav2PolicyFail_WarningCodes`

- `tools/vxavatar_quality_gate.ps1`
  - Added `Gate H` (`XAV2 unknown-section policy contract`).
  - Added strict field presence checks for policy probe fields.
  - Added policy contract checks:
    - `warn`/`ignore` primary error must be `NONE`.
    - `ignore` warning-code count must be `<= warn`.
    - `fail` primary error must be `NONE|XAV2_UNKNOWN_SECTION_NOT_ALLOWED`.
  - Added `gate_h` output to summary text and JSON.
  - Updated overall pass condition to include Gate H.

- `.github/workflows/vxavatar-gate.yml`
  - Expanded trigger paths to include:
    - `tools/avatar_tool.cpp`
    - `CMakeLists.txt`
  - Updated gate step names to explicit `(A-H)`.

- `README.md`
  - Added Gate H rule description under VXAvatar/VXA2/XAV2 quality gate.

## Verification

- Intended validation command:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick
```

- Expected output:
  - `build/reports/vxavatar_probe_latest.txt` contains `Xav2Policy*` fields.
  - `build/reports/vxavatar_gate_summary.txt` includes `GateH`.
  - `build/reports/vxavatar_gate_summary.json` includes `gates.gate_h`.
