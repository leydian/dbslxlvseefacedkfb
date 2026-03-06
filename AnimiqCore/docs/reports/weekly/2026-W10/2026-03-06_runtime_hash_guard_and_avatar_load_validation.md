# Runtime Hash Guard + Avatar Load Validation and JSON Contract (2026-03-06)

## Summary

This pass implements three reliability upgrades for avatar load operations:

1. Runtime provenance hardening now includes SHA256 hash identity (not only path/timestamp).
2. Host diagnostics and load-block UX now expose hash mismatch evidence directly.
3. `avatar_tool` now supports machine-readable JSON output, and a new batch script validates actual load outcomes (not file-size-only checks).

Primary goal: make stale/mismatched runtime binaries fail-closed earlier and produce actionable diagnostics for operators and gates.

## Problem Statement

Recent field symptoms showed that runtime load behavior could drift from CLI validation due to binary mismatch across launch paths.
Path and timestamp checks were useful but not sufficient in all scenarios:

- timestamp comparison can miss edge cases,
- path match alone does not prove binary identity,
- existing batch validation script (`avatar_batch_validate.ps1`) verified file presence/size but did not execute loader logic.

## Implemented Changes

### 1) HostCore runtime diagnostics contract: hash identity

Updated file:

- `host/HostCore/DiagnosticsModel.cs`

Changes:

- Added diagnostics fields:
  - `NativeCoreModuleSha256`
  - `BuildNativeCoreModuleSha256`
  - `ExpectedNativeCoreModuleSha256`
  - `RuntimeHashMatchExpected`
- Added local hash helper:
  - `ComputeSha256HexIfFileExists(...)`
- Updated stale decision policy:
  - prefer hash comparison between expected dist DLL and build output DLL,
  - set warning code `HOST_RUNTIME_DIST_HASH_MISMATCH_BUILD_OUTPUT` on mismatch,
  - keep timestamp fallback for environments lacking hash inputs.

Operational effect:

- runtime provenance check is now identity-based (content hash), not only location/time based.

### 2) WPF load-block and diagnostics visibility expansion

Updated file:

- `host/WpfHost/MainWindow.xaml.cs`

Changes:

- Runtime path mismatch block now includes:
  - loaded SHA256,
  - expected SHA256.
- Runtime stale block now includes:
  - loaded SHA256,
  - build SHA256,
  - expected SHA256.
- Runtime diagnostics static block/key now includes hash fields and `RuntimeHashMatchExpected`.

Operational effect:

- WPF operator sees concrete hash evidence in the same error surface where load is blocked.

### 3) WinUI load-block and diagnostics visibility expansion

Updated file:

- `host/WinUiHost/MainWindow.xaml.cs`

Changes:

- Same hash evidence wiring as WPF for runtime path mismatch/stale block messaging.
- Runtime diagnostics text now includes:
  - native/build/expected SHA256,
  - `RuntimeHashMatchExpected`.

Operational effect:

- parity with WPF for runtime provenance troubleshooting.

### 4) avatar_tool JSON contract for automation

Updated file:

- `tools/avatar_tool.cpp`

Changes:

- Added CLI options:
  - `--emit-json` (prints JSON to stdout)
  - `--json-out=<path>` (writes JSON artifact)
- Added JSON escaping utility (`JsonEscape`)
- JSON payload includes:
  - `loadSucceeded`, `path`, `displayName`, `format`, `compat`, `parserStage`, `primaryError`
  - `warningCodes[]`
  - `warningCodeMeta[]` (`severity`, `category`, `critical`)
  - `counts` block (mesh/material/payload/expression/warning aggregates)

Operational effect:

- gates and dashboards can parse structured output directly without brittle text parsing.

### 5) New real load-based batch validator

Added file:

- `tools/avatar_batch_load_validate.ps1`

Behavior:

- executes `avatar_tool` per matched avatar file,
- evaluates pass/fail based on actual loader outcome and policy flags,
- outputs both JSON and TXT summaries,
- defaults to strict checks (`runtime-ready`, `PrimaryError=NONE`, etc., unless explicitly relaxed).

Policy toggles:

- `-AllowPartialCompat`
- `-AllowFailedCompat`
- `-AllowNonRuntimeReady`
- `-AllowPrimaryError`

## Verification

Executed:

- `dotnet build host/HostCore/HostCore.csproj -c Release --no-restore` -> PASS
- `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore` -> PASS
- `cmake --build build_plan_impl --config Release --target avatar_tool` -> PASS
- `avatar_tool <sample.vrm> --json-out=build/reports/avatar_tool_test.json` -> PASS
- `tools/avatar_batch_load_validate.ps1 ... -IncludeExtensions .vrm` -> PASS

Artifacts generated:

- `build/reports/avatar_tool_test.json`
- `build/reports/avatar_batch_load_validation.vrm.json`
- `build/reports/avatar_batch_load_validation.vrm.txt`

## Known Limitations

- WinUI build in this environment is still blocked by NuGet/source configuration (`NU1301`) and not used as pass/fail signal for this specific change set.
- `tools/avatar_batch_validate.ps1` (size/existence-only validator) is intentionally retained for its original purpose; load-quality decisions should use `avatar_batch_load_validate.ps1`.

## Operator Guidance

- For runtime mismatch incidents, use host diagnostics hash fields first:
  - `NativeCoreModuleSha256`
  - `ExpectedNativeCoreModuleSha256`
  - `BuildNativeCoreModuleSha256`
- For CI/load-quality checks, use `avatar_tool --json-out` + `avatar_batch_load_validate.ps1` as canonical path.
