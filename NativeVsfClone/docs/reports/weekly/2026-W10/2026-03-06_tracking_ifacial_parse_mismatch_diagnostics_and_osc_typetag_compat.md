# 2026-03-06 Tracking iFacial Parse Mismatch Diagnostics and OSC TypeTag Compatibility

## Summary
This update hardens the iFacial OSC ingestion path so repeated `TrackingParseError: InvalidArgument` conditions are diagnosable and actionable without external packet inspection tools.

The host now:
- classifies parse failures into operator-meaningful causes (protocol mismatch vs unsupported type tag),
- surfaces explicit remediation hints in WPF/WinUI tracking status text,
- and accepts a wider set of OSC type tags often present in mixed OSC environments.

## Problem
- Observed runtime symptom: `parse_errors` and `dropped` counters increased together while packets were still being received.
- Existing diagnostics collapsed most failures into generic `TRACKING_PARSE_FAILED`, which slowed root-cause identification.
- In mixed sender environments, non-core OSC tags could trigger avoidable parser rejection.

## Implementation Details

### 1) Parse failure cause classification in receive loop
File: `host/HostCore/TrackingInputService.cs`

- `TryParsePacket(...)` now returns a structured parse-failure cause in addition to updates and format name.
- Receive-loop error handling maps causes to specific status and error codes:
  - `TRACKING_PROTOCOL_MISMATCH_VMC`
  - `TRACKING_OSC_TYPE_UNSUPPORTED`
  - existing generic `TRACKING_PARSE_FAILED`
- `SourceStatus` now carries cause suffixes for rapid triage:
  - `udp-parse-failed:vmc`
  - `udp-parse-failed:typetag`
  - threshold variants when warning limits are exceeded.

### 2) VMC protocol-mismatch detection
File: `host/HostCore/TrackingInputService.cs`

- Added lightweight heuristics:
  - packet/address prefix checks for `/VMC/` or `VMC/`.
- When detected, parser marks failure as `ProtocolMismatchVmc` so operators are prompted to switch sender mode to iFacial OSC.

### 3) OSC type-tag compatibility widening
File: `host/HostCore/TrackingInputService.cs`

- Extended message parser support:
  - `d` (64-bit double) -> converted to float,
  - `h` (64-bit integer) -> converted to float,
  - `T`, `F`, `N`, `I` (OSC special/no-payload tags) -> mapped to numeric sentinel values.
- Unsupported tags still fail safely with explicit `UnsupportedTypeTag`.

### 4) User-facing remediation hints
File: `host/HostCore/TrackingErrorHintCatalog.cs`

- Added new hints for:
  - `TRACKING_PARSE_FAILED`
  - `TRACKING_PROTOCOL_MISMATCH_VMC`
  - `TRACKING_OSC_TYPE_UNSUPPORTED`
- Hints are consumed by existing WPF/WinUI status rendering path via `BuildTrackingErrorHint(...)`.

## Validation
- Build verification completed for HostCore:
  - `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore`
  - result: `0 warnings`, `0 errors`.

## Expected Operator Impact
- Faster diagnosis of iFacial stream incompatibility cases.
- Reduced false-negative parse failures in mixed OSC setups.
- Clear in-app direction to correct sender mode/stream configuration without deep log forensics.

---

## Follow-up (2026-03-06): IFM v1/v2 Compatibility Fallback

### Why this follow-up was needed
- In field logs, `source_status=udp-parse-failed`, `format=unknown`, and `TRACKING_PARSE_FAILED` were observed while iFacialMocap was confirmed active and network reachability was otherwise healthy.
- Comparative runtime behavior showed VSeeFace receiving from iFacialMocap successfully in IFM mode, indicating a payload-shape compatibility gap rather than a pure network/bind issue.

### What was implemented
File: `host/HostCore/TrackingInputService.cs`

1) IFM parse fallback in `TryParsePacket(...)`
- Existing OSC-first behavior is preserved.
- When `TryParseOscMessage(...)` fails, parser now attempts IFM decoding before returning a hard parse failure.

2) IFM payload support (v1 + v2)
- Added fallback parser paths for:
  - JSON object payloads (including nested `blendshapes` object),
  - JSON array payloads (mapped by `Arkit52Channels.CanonicalOrder`),
  - delimited textual key/value payloads (`key=value`, `key:value`, CSV key/value pairs).
- Runtime format labeling extended with:
  - `ifm-v1`
  - `ifm-v2`

3) IFM key normalization and alias mapping
- Added canonicalization for common IFM aliases:
  - `blinkl` / `blinkleft` -> `eyeblinkleft`
  - `blinkr` / `blinkright` -> `eyeblinkright`
  - `mouthopen` / `visemeaa` / `aa` -> `jawopen`
  - `headrotation*` aliases -> `headyaw/headpitch/headroll`
- Mapping is constrained to known ARKit channels + supported pose channels to avoid accidental noise ingestion.

4) New IFM-specific parse diagnostics
- Added parse-failure categories:
  - `IfmMalformed`
  - `IfmUnsupportedVersion`
- Receive-loop status/error mappings added:
  - `udp-parse-failed:ifm-malformed` -> `TRACKING_IFM_MALFORMED`
  - `udp-parse-failed:ifm-version` -> `TRACKING_IFM_UNSUPPORTED_VERSION`

5) UI-facing hint enrichment
File: `host/HostCore/TrackingErrorHintCatalog.cs`
- Added explicit remediation hints for:
  - `TRACKING_IFM_MALFORMED`
  - `TRACKING_IFM_UNSUPPORTED_VERSION`

### Compatibility and risk notes
- OSC behavior is intentionally left intact; IFM is additive fallback, not replacement.
- VMC mismatch detection (`TRACKING_PROTOCOL_MISMATCH_VMC`) remains unchanged.
- No public API contract changes were introduced for host UI; diagnostics surface through existing fields.

### Verification (follow-up slice)
- `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore`
- Result: `0 warnings`, `0 errors`.

### Redeploy + receive-check automation
- Added wrapper script:
  - `tools/redeploy_wpf_ifacial_check.ps1`
- Purpose:
  - run WPF redeploy via existing `publish_hosts.ps1`,
  - launch `dist/wpf/WpfHost.exe`,
  - observe tracking status and judge iFacial receive success with:
    - PASS: `packets` increases and `parse_err` does not increase.
- Output artifacts:
  - text report: `build/reports/ifacial_redeploy_check_latest.txt`
  - JSON summary: `build/reports/ifacial_redeploy_check_latest.json`
- Example:
  - `powershell -ExecutionPolicy Bypass -File .\tools\redeploy_wpf_ifacial_check.ps1 -Configuration Release -RuntimeIdentifier win-x64`

### Script contract details (operational)
1) Inputs
- `-Configuration` (default `Release`)
- `-RuntimeIdentifier` (default `win-x64`)
- `-NoRestore`
- `-SkipPublish`
- `-AutoLaunch` (default `true`)
- `-NonInteractive` (switch; disables prompt-based fallback)
- `-ObserveSeconds` (default `25`)
- `-PollIntervalMs` (default `1000`)
- `-ReportPath` (default `.\build\reports\ifacial_redeploy_check_latest.txt`)

2) Execution flow
- Triggers `tools/publish_hosts.ps1` for WPF-only redeploy unless `-SkipPublish` is set.
- Resolves and launches `dist/wpf/WpfHost.exe` (or reuses running process when `-AutoLaunch:$false`).
- Collects tracking status samples from WPF status text (`tracking=... packets=... parse_err=...`) via UI Automation.
- If UI Automation fails in interactive mode, accepts manual status-line paste twice and computes deltas.

3) PASS/FAIL policy
- PASS:
  - `packets_delta > 0`
  - `parse_err_delta <= 0`
- FAIL:
  - no parsable tracking samples,
  - `packets_delta <= 0`,
  - or `parse_err_delta > 0`.

4) Outputs
- Text report:
  - `build/reports/ifacial_redeploy_check_latest.txt`
- JSON summary:
  - `build/reports/ifacial_redeploy_check_latest.json`
- Captured fields include:
  - sample count, first/last counters, deltas, final status, reason, and last tracking line when available.

5) Known limitations
- UI Automation text scraping can fail depending on host focus/rendering environment.
- Interactive fallback (manual status-line paste) exists for that case and is intentionally retained for operator reliability.
