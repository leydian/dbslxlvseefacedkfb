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
