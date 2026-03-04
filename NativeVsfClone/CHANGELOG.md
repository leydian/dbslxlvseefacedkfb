# Changelog

All notable implementation changes in this workspace are documented here.

## 2026-03-05 - WPF-first transition detailed report publication

### Summary

Published a dedicated transition report that documents the final WPF-first operating contract, CI split, and latest verification evidence after the policy switch implementation.

### Changed

- `docs/reports/host_wpf_first_transition_2026-03-05.md` (new)
  - includes:
    - rationale for WPF-first switch
    - script/gate/CI contract changes
    - latest artifact timestamps and verification outcomes
    - operational impact and remaining WinUI recovery item

- `docs/INDEX.md`
  - added report index entry for `host_wpf_first_transition_2026-03-05.md`

## 2026-03-05 - WPF-first host policy transition (WinUI optional diagnostics track)

### Summary

Shifted host publish/gate/CI contracts to WPF-first operation so release gating is stable while WinUI remains available as an opt-in diagnostics track.

### Changed

- `tools/publish_hosts.ps1`
  - added explicit host publish mode logging:
    - `WPF_ONLY` (default)
    - `WPF_PLUS_WINUI` (`-IncludeWinUi`)
  - updated WinUI skip log message to clarify optional diagnostics role in default mode

- `tools/vsfavatar_quality_gate.ps1`
  - HostTrack auto-resolution now treats WPF output as a pass baseline:
    - `PASS_WPF_BASELINE`
    - `PASS_WPF_AND_WINUI`
  - HostTrack DoD pass check updated from strict `PASS` to `PASS*` family

- `tools/vsfavatar_sample_report.ps1`
  - HostTrack DoD pass check updated to `PASS*` family for report parity

- `.github/workflows/host-publish.yml`
  - split host publish jobs into:
    - required WPF-first publish job (`publish-hosts-wpf`)
    - optional/non-blocking WinUI diagnostics job (`publish-hosts-winui-diagnostics`)
  - WPF required artifact validation now checks WPF outputs + publish report only

- `README.md`
  - documented WPF-first policy and `PASS*` HostTrack contract

- `docs/reports/host_change_rollup_2026-03-05.md`
  - appended policy transition section and CI split summary

## 2026-03-05 - Host rollup detail refresh (commit breakdown + latest snapshot)

### Summary

Expanded the consolidated host rollup document with commit-by-commit detail and latest verified runtime snapshot timestamps so operators can map current status directly to recent commits and artifacts.

### Changed

- `docs/reports/host_change_rollup_2026-03-05.md`
  - added detailed breakdown for:
    - `5fabc34`
    - `08b3fb8`
    - `067d0dd`
    - `ccaefc5`
  - added latest verification snapshot section with artifact generation times and final state summary (`preflight=PASS`, `failure_class=TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`)

## 2026-03-05 - WinUI preflight unblock confirmation (Windows SDK 19041 installed)

### Summary

Executed the host/gate/baseline plan again after installing Windows SDK `10.0.19041` to remove the preflight blocker and verify state transition behavior.

- WinUI preflight transitioned from `FAIL` to `PASS`
- WinUI publish now fails at XAML compile stage (post-preflight), not precondition stage
- failure class transitioned from `TOOLCHAIN_PRECONDITION_FAILED` to `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
- HostTrack auto-resolution transitioned to `BLOCKED_XAML_PLATFORM_UNSUPPORTED`
- quality baseline remained `PASS`

### Environment update

- installed package:
  - `Microsoft.WindowsSDK.10.0.19041` (winget)
- metadata probe confirmation:
  - `C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.19041.0\Facade\Windows.winmd` => present

### Verification

- `dotnet --version`
  - `8.0.418`

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI preflight: PASS
  - WinUI publish: FAIL (`XamlCompiler.exe` / `MSB3073`)
  - diagnostics class: `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`

- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
  - ParserTrack_DoD: PASS
  - HostTrackStatus: `BLOCKED_XAML_PLATFORM_UNSUPPORTED`

- `powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1`
  - Overall: PASS

## 2026-03-05 - Host change rollup document update

### Summary

Added a consolidated operator-facing rollup that summarizes the latest diagnostics schema hardening and re-validation outcomes in one place.

### Changed

- `docs/reports/host_change_rollup_2026-03-05.md` (new)
  - unified summary of:
    - `preflight_probe` manifest expansion
    - failure-class precedence hardening
    - latest verified runtime status (`WPF=PASS`, `WinUI preflight blocked`)
    - explicit unblock action and success condition

- `docs/INDEX.md`
  - added report index entry for the new rollup document

## 2026-03-05 - Host plan execution re-validation (post-push confirmation)

### Summary

Executed the planned host/gate/baseline sequence again after pushing diagnostics/schema hardening changes to confirm there was no regression and blocker state remained deterministic.

- `dotnet --version` in repo context: `8.0.418` (`global.json` pin confirmed)
- WinUI preflight blocker remains unchanged: `MISSING_WINDOWS_SDK_19041_METADATA`
- manifest class remains deterministic: `TOOLCHAIN_PRECONDITION_FAILED`
- HostTrack remains auto-resolved to `BLOCKED_TOOLCHAIN_PRECONDITION`
- quality baseline remains `PASS`

### Verification

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI preflight: FAIL (`MISSING_WINDOWS_SDK_19041_METADATA`)
  - diagnostics manifest: `build/reports/winui/winui_diagnostic_manifest.json`

- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
  - ParserTrack_DoD: PASS
  - HostTrackStatus: `BLOCKED_TOOLCHAIN_PRECONDITION`

- `powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1`
  - Overall: PASS

## 2026-03-05 - WinUI diagnostics schema expansion (preflight probe + class precedence)

### Summary

Extended WinUI diagnostics output so preflight evidence is decision-complete in the manifest and aligned failure classification priority with current host-track expectations.

- added `preflight_probe` field in WinUI diagnostics manifest
- kept `preflight` legacy shape unchanged for downstream compatibility
- added explicit probe evidence for .NET SDK, Visual Studio discovery, and Windows SDK 19041 metadata paths
- enforced failure classification priority to prefer `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED` over generic XAML exec failures when managed `WMC9999` is present

### Changed

- `tools/publish_hosts.ps1`
  - diagnostic manifest:
    - added `preflight_probe`
    - preserved legacy `preflight` object contract (`passed`, `failed_checks`, `detected_sdks`, `recommended_actions`)
  - preflight probing:
    - added explicit probe checks and path evidence for Windows SDK metadata candidates:
      - `UnionMetadata\10.0.19041.0\Facade\Windows.winmd`
      - `References\10.0.19041.0\Windows.Foundation.FoundationContract...winmd`
  - failure classification:
    - documented/evaluated priority as:
      - `TOOLCHAIN_PRECONDITION_FAILED` (and `TOOLCHAIN_MISSING_DOTNET8` specialization)
      - `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED`
      - diagnostics-driven classes (`NUGET_SOURCE_UNREACHABLE`, `XAML_COMPILER_EXEC_FAIL`, etc.)
      - `UNKNOWN`

### Verification

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI preflight: FAIL (`MISSING_WINDOWS_SDK_19041_METADATA`)
  - `winui_diagnostic_manifest.json` includes:
    - `failure_class=TOOLCHAIN_PRECONDITION_FAILED`
    - `preflight` legacy fields
    - `preflight_probe.checks[*]` with checked metadata paths and detection flags

- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
  - ParserTrack_DoD: PASS
  - HostTrackStatus: `BLOCKED_TOOLCHAIN_PRECONDITION`
  - HostTrack resolved from manifest `failure_class`

## 2026-03-05 - WinUI preflight hardening follow-up (SDK pin + Windows SDK metadata gate)

### Summary

Applied an additional hardening pass after the 2026-03-05 execution round to make the WinUI blocker fail-fast and deterministic in this environment:

- pinned workspace CLI SDK to .NET 8 (`global.json`)
- added WinUI preflight guard for missing `Windows SDK 10.0.19041` metadata facade
- refined failure classification for managed `WMC9999` path
- verified host track auto-resolution transitions to precondition-blocked status

### Changed

- `global.json` (new)
  - pins SDK to `8.0.418` (`rollForward=latestFeature`)

- `tools/publish_hosts.ps1`
  - preflight:
    - added `MISSING_WINDOWS_SDK_19041_METADATA` check
  - failure class:
    - added `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED` mapping for managed `WMC9999`

- `tools/vsfavatar_quality_gate.ps1`
  - added manifest class mapping:
    - `TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED` -> `BLOCKED_XAML_PLATFORM_UNSUPPORTED`

- `docs/reports/host_execution_round_2026-03-05.md`
  - appended post-hardening follow-up outcome section.

### Verification

- `dotnet --version` (in repo root with `global.json`)
  - `8.0.418`

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI preflight: FAIL (`MISSING_WINDOWS_SDK_19041_METADATA`)
  - diagnostics class: `TOOLCHAIN_PRECONDITION_FAILED`

- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
  - ParserTrack_DoD: PASS
  - HostTrackStatus: `BLOCKED_TOOLCHAIN_PRECONDITION`

## 2026-03-05 - WinUI blocker execution round (SDK8 remediation + XAML failure-class confirmation)

### Summary

Executed the current host/gate plan sequence and captured updated blocker state:

- quality baseline re-run: PASS
- WinUI preflight blocker (`MISSING_DOTNET_8_SDK`) resolved by installing .NET 8 SDK
- WinUI publish still fails at XAML compile stage, now consistently classified as `XAML_COMPILER_EXEC_FAIL`
- HostTrack auto-resolution correctly maps diagnostics manifest class to `BLOCKED_XAML_COMPILER`

### Changed

- `host/WinUiHost/WinUiHost.csproj`
  - Upgraded `Microsoft.WindowsAppSDK`:
    - `1.5.240802000` -> `1.8.260209005`

- `docs/reports/host_execution_round_2026-03-05.md` (new)
  - Added full execution record:
    - baseline/gate commands and results
    - SDK8 remediation step
    - WinUI publish failure-class evidence (`MSB3073`, `WMC9999`)
    - HostTrack auto-resolution verification

- `docs/INDEX.md`
  - Added link to `host_execution_round_2026-03-05.md`.

### Verification

- `powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1`
  - Overall: PASS

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI publish: FAIL
  - diagnostics class: `XAML_COMPILER_EXEC_FAIL`

- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
  - ParserTrack_DoD: PASS
  - HostTrackStatus: `BLOCKED_XAML_COMPILER`

## 2026-03-04 - WinUI preflight fail-fast + HostTrack auto-resolution + host/baseline CI integration

### Summary

Implemented the follow-up execution plan to reduce WinUI publish ambiguity and make HostTrack reporting/CI verification deterministic:

- added WinUI toolchain preflight with fail-fast behavior
- added normalized WinUI failure-class output in diagnostics manifest
- switched VSFAvatar HostTrack from static default to evidence-based auto-resolution
- integrated quality-baseline job into host publish workflow (`pull_request` + `push(main)`)

### Changed

- `tools/publish_hosts.ps1`
  - Added WinUI preflight checks before publish:
    - `.NET 8 SDK` presence
    - Visual Studio discovery availability
  - Added fail-fast behavior for preflight failures with diagnostics capture.
  - Added normalized failure classification and confidence:
    - `TOOLCHAIN_MISSING_DOTNET8`
    - `TOOLCHAIN_PRECONDITION_FAILED`
    - `NUGET_SOURCE_UNREACHABLE`
    - `MANAGED_XAML_TASK_MISSING_DEP`
    - `XAML_COMPILER_EXEC_FAIL`
    - `UNKNOWN`
  - Extended WinUI diagnostic manifest fields:
    - `failure_class`
    - `failure_class_confidence`
    - `preflight` (`passed`, `failed_checks`, `detected_sdks`, `recommended_actions`)

- `tools/vsfavatar_quality_gate.ps1`
  - Changed default `HostTrackStatus` from static blocker to `AUTO`.
  - Added host evidence inputs:
    - `HostPublishReportPath`
    - `WinUiDiagnosticManifestPath`
  - Added auto-resolution logic for host track status/reason/evidence path.
  - Updated HostTrack DoD contract:
    - `PASS` only when resolved status is `PASS`.

- `tools/vsfavatar_sample_report.ps1`
  - Added host metadata fields to report header:
    - `HostTrackStatusReason`
    - `HostTrackEvidencePath`
  - Updated HostTrack DoD line to align with `HostTrackStatus=PASS` contract.

- `.github/workflows/host-publish.yml`
  - Added `quality-baseline` job (`tools/run_quality_baseline.ps1`).
  - Set `publish-hosts` to depend on baseline job.
  - Expanded path triggers to include baseline/gate scripts.
  - Added baseline artifact upload bundle.

- `docs/reports/host_runtime_parity_smoke_checklist_2026-03-04.md` (new)
  - Added post-publish WPF/WinUI runtime parity smoke checklist and result template.

- `README.md`
  - Updated WinUI publish notes for preflight fail-fast + enriched manifest schema.
  - Updated VSFAvatar HostTrack DoD wording to `HostTrackStatus=PASS`.

- `docs/INDEX.md`
  - Added link to `host_runtime_parity_smoke_checklist_2026-03-04.md`.

## 2026-03-04 - Host publish diagnostics hardening + gate baseline re-validation

### Summary

Executed the planned verification round after recent host/parser updates:

- re-ran VSFAvatar/VRM/VXAvatar gates and re-confirmed PASS baseline
- re-ran host publish path and confirmed `WPF=PASS`, `WinUI=FAIL`
- hardened WinUI failure diagnostics in publish script to extract actionable root-cause hints

### Changed

- `tools/publish_hosts.ps1`
  - Added `CollectManagedXamlDiagnostics` option (default `true`).
  - On WinUI publish failure, now runs an additional managed XAML diagnostic pass:
    - `dotnet build ... -p:UseXamlCompilerExecutable=false`
  - Added root-cause hint extraction from diagnostics logs:
    - `XamlCompiler.exe` MSB3073 path detection
    - `NU1101` / `NU1301` NuGet failure detection
    - `System.Security.Permissions` missing assembly detection
    - `WMC9999` platform-unsupported/internal compiler error detection
    - `.NET 8 SDK` precondition hint when only `9.x` SDK entries are detected
  - Extended diagnostic manifest with:
    - managed diagnostic command/exit code
    - managed diagnostic log paths
    - extracted root-cause hints
  - Expanded WinUI obj diagnostic dump to include `input.json` in addition to `output.json`/log files.

- `README.md`
  - Added latest validation snapshot (`2026-03-04`) with gate and publish status.
  - Updated GUI publish notes with managed XAML diagnostic artifact paths.
  - Added combined baseline command documentation (`tools/run_quality_baseline.ps1`).

- `tools/run_quality_baseline.ps1` (new)
  - Added combined quality baseline runner for standard gate set:
    - VSFAvatar fixed-set
    - VRM fixed5
    - VXAvatar quick fixed-set
  - Added summary output:
    - `build/reports/quality_baseline_summary.txt`

- `docs/reports/host_gate_execution_round_2026-03-04.md` (new)
  - Added execution report for gate re-validation and host publish results.

- `docs/INDEX.md`
  - Added link to `host_gate_execution_round_2026-03-04.md`.

### Verification

- `powershell -ExecutionPolicy Bypass -File .\tools\vsfavatar_quality_gate.ps1 -UseFixedSet`
  - GateA/B/C/D: PASS
  - Overall: PASS

- `powershell -ExecutionPolicy Bypass -File .\tools\vrm_quality_gate.ps1 -Profile fixed5`
  - Overall: PASS

- `powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`
  - GateA..H: PASS
  - Overall: PASS

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -IncludeWinUi`
  - WPF publish: PASS
  - WinUI publish: FAIL (`XamlCompiler.exe` exit code `1`)
  - diagnostics captured under `build/reports/winui`

- `powershell -ExecutionPolicy Bypass -File .\tools\run_quality_baseline.ps1`
  - Overall: PASS

## 2026-03-03 - VSFAvatar serialized bottleneck follow-up (4/4 fixed samples complete)

### Summary

Completed a follow-up pass focused on the remaining serialized parsing bottleneck after the LZMA/reconstruction lift. The fixed 4-sample set now reaches `complete + object_table_parsed=true + primary_error_code=NONE` for all samples, while preserving gate stability.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added additive serialized diagnostics fields:
    - `serialized_detail_error_code`
    - `serialized_last_failure_offset`
    - `serialized_last_failure_window_size`
    - `serialized_last_failure_code`

- `src/vsf/unityfs_reader.cpp`
  - Expanded node-based serialized candidate attempts with wider offset deltas and low-candidate expansion windows.
  - Increased minimum parse window for node candidates to reduce header/window miss cases.
  - Added structured tracking for best serialized failure detail (code/offset/window).
  - Added raw-scan failure detail mapping (`SF_NO_RAW_HEADER_CANDIDATE` / `SF_RAW_PARSE_FAILED`).

- `src/vsf/serialized_file_reader.cpp`
  - Added parse classification split for truncated metadata windows:
    - `SF_METADATA_WINDOW_TRUNCATED`
  - Tuned offset-scan behavior for large buffers:
    - reduced scan limit / hit cap / window size
    - larger step for large buffers to avoid runtime blow-up while preserving fallback recovery.

- `tools/vsfavatar_sidecar.cpp`
  - Emitted new optional serialized detail fields.
  - Added `W_SERIALIZED_DETAIL` warning line for easier triage.

- `src/avatar/vsfavatar_loader.cpp`
  - Consumed serialized detail fields as warnings only (no schema contract break).

- `docs/reports/vsfavatar_serialized_bottleneck_followup_2026-03-03.md` (new)
  - Added follow-up implementation + verification report.

### Verification

- `tools/vsfavatar_sample_report.ps1 -UseFixedSet:$true ...`
  - `UseFixedSet=True`, `FileCount=4`, `GateRows=4`.
  - All fixed samples reported `SidecarProbeStage=complete`, `SidecarObjectTableParsed=True`, `SidecarPrimaryError=NONE`.

- `tools/vsfavatar_quality_gate.ps1 -UseFixedSet -ReportPath ./build/reports/vsfavatar_probe_latest_after_gate_new.txt`
  - GateA/B/C/D: PASS
  - Overall: PASS

- `tools/vrm_quality_gate.ps1`
  - Overall: PASS

## 2026-03-03 - VSFAvatar gate reporting split (parser/host) + aggregate diagnostics

### Summary

Improved VSFAvatar gate operations by splitting Parser/Host track status, adding aggregate diagnostics outputs, and formalizing DoD-oriented reporting fields for iterative GateD work.

### Changed

- `tools/vsfavatar_sample_report.ps1`
  - Added `HostTrackStatus` input and report header output.
  - Added `ParserTrack_DoD` / `HostTrack_DoD` summary lines.
  - Added `RunDurationSec` output for run-to-run timing comparison.

- `tools/vsfavatar_quality_gate.ps1`
  - Added Parser/Host track split in gate summary.
  - Added smoke mode (`-UseSmoke -SmokeMaxFiles N`) for fast pre-gate loops.
  - Added run metrics:
    - `RunDurationSec`
    - `SerializedAttempts_Avg`
    - `SerializedAttempts_Max`
  - Added aggregate outputs:
    - `build/reports/vsfavatar_gate_aggregate.csv`
    - `build/reports/vsfavatar_gate_aggregate.txt`
  - Added stage/primary/object-table distributions for failure triage.
  - Baseline report is now optional (missing baseline continues with empty diff base).

- `README.md`
  - Updated VSFAvatar quality-gate section with track split and new aggregate outputs.
  - Added gate-work commit hygiene guidance.

- `docs/reports/vsfavatar_serialized_gateD_update_2026-03-03.md`
  - Added DoD status checklist for Parser/Host tracks.

## 2026-03-03 - Render/preset busy-gating parity follow-up for WPF and WinUI

### Summary

Completed a follow-up pass to enforce busy-state safety on render and preset interactions, aligning WPF and WinUI behavior with the same host operation gating contract.

### Changed

- `host/WpfHost/MainWindow.xaml.cs`
  - Added `ShouldSkipRenderInteraction()` helper (`_isSyncingRenderUi || OperationState.IsBusy`).
  - Applied busy-aware guard to render interaction handlers:
    - `BroadcastMode_Changed`
    - `CameraMode_SelectionChanged`
    - slider/preset background/mirror/debug handlers
  - Added busy guard for preset actions:
    - `SavePreset_Click`
    - `ApplyPreset_Click`
    - `DeletePreset_Click`
    - `ResetRender_Click`
  - Updated render control enable rules:
    - `Yaw`/`FOV` enabled only in `Manual` camera mode.
  - Updated camera mode change flow to refresh UI enable state immediately before queued apply.

- `host/WinUiHost/MainWindow.xaml.cs`
  - Added same `ShouldSkipRenderInteraction()` helper and handler coverage as WPF.
  - Added same preset busy guards as WPF.
  - Updated render control enable rules with same manual-mode condition for `Yaw`/`FOV`.
  - Updated camera mode change flow to refresh UI enable state before queued apply.

- `docs/reports/ui_render_busy_gating_parity_2026-03-03.md` (new)
  - Added implementation and parity behavior report for this follow-up.

- `docs/INDEX.md`
  - Added link to the new render busy-gating parity report.

### Verification

- `dotnet build host/HostCore/HostCore.csproj -c Release`
  - PASS
- WPF/WinUI host build:
  - not re-validated in this follow-up run due environment network dependency for restore/package resolution.

## 2026-03-03 - Host UI input validation/busy-state gating + WinUI diagnostic environment snapshot

### Summary

Improved host operation safety and observability across WPF/WinUI by adding shared HostCore validation and busy-operation contracts, wiring UI action gating to those contracts, and enriching WinUI publish-failure diagnostics with environment metadata for reproducible triage.

### Changed

- `host/HostCore/HostUiState.cs`
  - Added:
    - `HostOperationState` (`IsBusy`, `CurrentOperation`)
    - `HostValidationState` (`AvatarPathValid`, `OscBindPortValid`, `OscPublishAddressValid`, error texts)

- `host/HostCore/HostController.cs`
  - Added:
    - `OperationState` property
    - `ValidateInputs(avatarPath, oscBindPortText, oscPublishAddress)` API
    - centralized operation wrapper for busy-state transitions on mutating actions
  - Added validation helpers:
    - avatar path checks (required, supported extension, file existence)
    - OSC bind port checks (`ushort`)
    - OSC publish address checks (`host:port` + `ushort` port)

- `host/WpfHost/MainWindow.xaml`
  - Added inline validation text blocks for avatar path and OSC inputs.
  - Added `TextChanged` hooks on avatar/OSC input text boxes.
  - Added `Busy` field to status strip.
  - Added horizontal scroll support for logs view.

- `host/WpfHost/MainWindow.xaml.cs`
  - Added live validation refresh and inline error rendering.
  - Added busy-guard short-circuit on lifecycle/output actions.
  - Updated action enable rules to include:
    - session/avatar state
    - validation state (`Load`, `Start OSC`)
    - busy state (`!IsBusy`)

- `host/WinUiHost/MainWindow.xaml`
  - Added left-panel `ScrollViewer` for small-window operability.
  - Added inline validation blocks + `TextChanged` hooks.
  - Updated logs textbox to `NoWrap` + horizontal scroll.
  - Added `Busy` status line.

- `host/WinUiHost/MainWindow.xaml.cs`
  - Added live validation refresh and inline error rendering.
  - Added busy-guard short-circuit on lifecycle/output actions.
  - Updated action enable rules with same gate model as WPF.

- `tools/publish_hosts.ps1`
  - Extended WinUI diagnostic manifest with environment capture:
    - OS version
    - `.NET SDKs`/`runtimes`
    - Visual Studio discovery (`vswhere`, when available)

- `docs/reports/ui_host_validation_busy_and_winui_diag_2026-03-03.md` (new)
  - Added detailed implementation and verification report.

- `docs/INDEX.md`
  - Added link to the new host validation/busy/diagnostics report.

### Verification

- `dotnet build host/HostCore/HostCore.csproj -c Release`
  - PASS
- `dotnet build host/WpfHost/WpfHost.csproj -c Release`
  - BLOCKED in this environment by NuGet/network access (`NU1301`/`NU1101`)
- `dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64`
  - BLOCKED in this environment by NuGet/network access (`NU1301`/`NU1101`)

## 2026-03-03 - VXAvatar/VXA2/XAV2 Gate H expansion for XAV2 policy contract

### Summary

Extended the VXAvatar/VXA2/XAV2 gate harness with a new Gate H to continuously validate native XAV2 unknown-section policy behavior and warning-code count contract (`warn|ignore|fail`) in both local runs and CI.

### Changed

- `tools/vxavatar_sample_report.ps1`
  - Added policy probe fields for XAV2 rows:
    - `Xav2PolicyWarn_PrimaryError`
    - `Xav2PolicyWarn_WarningCodes`
    - `Xav2PolicyIgnore_PrimaryError`
    - `Xav2PolicyIgnore_WarningCodes`
    - `Xav2PolicyFail_PrimaryError`
    - `Xav2PolicyFail_WarningCodes`

- `tools/vxavatar_quality_gate.ps1`
  - Added Gate H (`XAV2 unknown-section policy contract`).
  - Added policy field presence checks and numeric parsing checks.
  - Added policy assertions:
    - `warn/ignore` primary error must be `NONE`.
    - `ignore` warning-code count must be `<= warn`.
    - `fail` primary error must be `NONE|XAV2_UNKNOWN_SECTION_NOT_ALLOWED`.
  - Included `gate_h` in summary text/JSON and overall pass condition.

- `.github/workflows/vxavatar-gate.yml`
  - Added trigger paths:
    - `tools/avatar_tool.cpp`
    - `CMakeLists.txt`
  - Updated quick/full step labels to explicit `(A-H)`.

- `README.md`
  - Added Gate H rule description in VXAvatar/VXA2/XAV2 quality gate section.

- `docs/reports/xav2_policy_gateH_ci_2026-03-03.md` (new)
  - Added implementation and expected verification output summary.

- `docs/INDEX.md`
  - Added link to Gate H expansion report.

### Verification

- `powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`
  - PASS

## 2026-03-03 - XAV2 native parity for unknown-section policy + structured warning codes

### Summary

Aligned native C++ XAV2 loader diagnostics with the Unity SDK policy model by introducing configurable unknown-section handling (`Warn|Ignore|Fail`) and normalized warning-code surfaces.

### Changed

- `include/vsfclone/avatar/avatar_package.h`
  - Added `Xav2UnknownSectionPolicy` enum.
  - Added `AvatarPackage.warning_codes`.

- `include/vsfclone/avatar/avatar_loader_facade.h`
  - Added `AvatarLoadOptions` with `xav2_unknown_section_policy`.
  - Added `Load(path, options)` overload (existing `Load(path)` preserved).

- `src/avatar/avatar_loader_facade.cpp`
  - Routed XAV2 loader calls through policy-aware overload while keeping non-XAV2 behavior unchanged.

- `src/avatar/xav2_loader.h`
  - Added `Load(path, Xav2UnknownSectionPolicy)` overload.

- `src/avatar/xav2_loader.cpp`
  - Added warning-code extraction and accumulation into `warning_codes`.
  - Added unknown-section policy behavior:
    - `Warn`: emit `XAV2_UNKNOWN_SECTION` warning.
    - `Ignore`: skip warning.
    - `Fail`: stop with `XAV2_UNKNOWN_SECTION_NOT_ALLOWED`.
  - Kept default `Load(path)` behavior by delegating to `Warn`.

- `tools/avatar_tool.cpp`
  - Switched to `AvatarLoaderFacade` direct load path.
  - Added CLI option:
    - `--xav2-unknown-section-policy=warn|ignore|fail`
  - Added diagnostics output fields:
    - `WarningCodes`
    - `LastWarningCode`

- `CMakeLists.txt`
  - Updated `avatar_tool` link target from `nativecore` to `vsfclone_core`.

- `docs/formats/xav2.md`
  - Documented native unknown-section policy and `warning_codes[]`.

- `README.md`
  - Updated Unity/native XAV2 diagnostics section to include unknown-section policy + warning-code fields.

### Verification

- `cmake --build build --config Release --target avatar_tool`
  - PASS
- `.\build\Release\avatar_tool.exe .\build\tmp_vx\demo_mvp.xav2 --xav2-unknown-section-policy=warn`
  - PASS

## 2026-03-03 - Host publish script hardening for restore/network-failure diagnostics

### Summary

Hardened host publish behavior to fail on real `dotnet` exit codes, preserve WinUI diagnostics even when WPF publish fails first, and support explicit no-restore validation runs.

### Changed

- `tools/publish_hosts.ps1`
  - Added `-NoRestore` switch.
  - Added `Invoke-DotNetCommand` helper to enforce non-zero exit failure handling for `dotnet publish`.
  - Normalized `WinUiDiagDir` to absolute path.
  - Updated flow so WPF publish failure is logged but does not block WinUI diagnostic capture when `-IncludeWinUi` is enabled.
  - WinUI diagnostic build now mirrors `--no-restore` when selected.

- `docs/reports/host_stabilization_round_2026-03-03.md` (new)
  - Added host stabilization execution report including command outcomes and blocker status.

- `docs/INDEX.md`
  - Added link to host stabilization round report.

### Verification

- `dotnet restore host/WpfHost/WpfHost.csproj -v minimal`
  - PASS
- `dotnet restore host/WinUiHost/WinUiHost.csproj -v minimal`
  - PASS
- `dotnet build host/WpfHost/WpfHost.csproj -c Release --no-restore`
  - PASS
- `dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64 --no-restore`
  - FAIL (`MSB3073`, `XamlCompiler.exe ... output.json`, exit code `1`)
- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi`
  - FAIL (WinUI publish step), but WinUI diagnostics files generated successfully under `build/reports/winui`.
- Additional mitigation attempts:
  - WinUI `obj/bin` clean rebuild and direct `XamlCompiler.exe` run were both reproduced as fail (`exit=1`) without line-level diagnostics.

## 2026-03-03 - WinUI publish failure diagnostics + VSFAvatar serialized failure-detail propagation

### Summary

Added deterministic WinUI publish failure diagnostics (local script + CI artifact path) and expanded VSFAvatar serialized parsing diagnostics to preserve compact failure code and last failure tuple (`offset/window/code`) across probe -> sidecar -> loader warning flow.

### Changed

- `tools/publish_hosts.ps1`
  - Added parameters:
    - `CollectWinUiDiagnostics` (default: `true`)
    - `WinUiDiagDir` (default: `.\build\reports\winui`)
  - Wrapped WinUI publish in `try/catch`.
  - On WinUI publish failure, now runs diagnostic build:
    - `dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64 -v:diag -bl:<binlog>`
  - Collects and stores:
    - `winui_build.binlog`
    - `winui_build_diag.log`
    - `winui_build_stderr.log`
    - `winui_diagnostic_manifest.json`
    - `obj-dump/**` copied from `host/WinUiHost/obj`
  - Emits diagnostic artifact paths into `build/reports/host_publish_latest.txt`.

- `.github/workflows/host-publish.yml`
  - Updated publish step to include WinUI path:
    - `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1 -SkipNativeBuild -IncludeWinUi`
  - Expanded artifact upload set to include:
    - `NativeVsfClone/build/reports/winui`
  - Kept `if: always()` artifact upload behavior.

- `src/vsf/serialized_file_reader.cpp`
  - Added error classification for truncated metadata windows:
    - `SF_METADATA_WINDOW_TRUNCATED`
  - Refined big-endian parse error messages to distinguish:
    - invalid size (`invalid metadata size`)
    - oversized-in-window (`metadata size exceeds current window`)
    - range truncation (`metadata window truncated`)
  - Relaxed raw header pre-check and widened scan:
    - step `8 -> 4`
    - max hits `512 -> 2048`

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added additive probe fields:
    - `serialized_detail_error_code`
    - `serialized_last_failure_offset`
    - `serialized_last_failure_window_size`
    - `serialized_last_failure_code`

- `src/vsf/unityfs_reader.cpp`
  - Expanded node-candidate fallback with bounded nearby expansion for sparse candidate sets.
  - Increased node offset delta search set up to `±4096`.
  - Added minimum parse window floor (`512 KiB`) for candidate attempts.
  - Persisted best serialized failure tuple and compact detail code into probe fields.
  - Mirrored the same detail tuple handling in raw bundle serialized scan path.
  - Clears new serialized detail fields on successful parse for consistency.

- `tools/vsfavatar_sidecar.cpp`
  - Added serialized detail warning line:
    - `W_SERIALIZED_DETAIL: code=..., last-offset=..., window=..., last-code=...`
  - Emitted additive JSON fields:
    - `serialized_detail_error_code`
    - `serialized_last_failure_offset`
    - `serialized_last_failure_window_size`
    - `serialized_last_failure_code`

- `src/avatar/vsfavatar_loader.cpp`
  - Consumes new sidecar serialized detail fields and appends warning:
    - `W_SERIALIZED_DETAIL: ...`

- `docs/reports/winui_xaml_diagnostics_artifacts_2026-03-03.md` (new)
  - Added WinUI diagnostic artifact map and triage order.

- `docs/INDEX.md`
  - Added link to the WinUI diagnostics artifact guide report.

### Verification

- Script syntax check:
  - `tools/publish_hosts.ps1`: parse OK
- Full WinUI publish success/failure runtime verification in this pass:
  - not executed (pending environment run).

## 2026-03-03 - VSFAvatar GateD pass with UnityFS LZMA block decode and reconstruction scoring updates

### Summary

Lifted VSFAvatar fixed-set quality gate from GateD FAIL to PASS by implementing `mode=1` (LZMA) UnityFS block decode, adjusting block0 candidate/ranking logic, and extending sidecar/loader diagnostics in an additive-compatible way.

### Changed

- `src/vsf/unityfs_reader.cpp`
  - Added UnityFS `mode=1` LZMA decode path via integrated `LzmaDecode` one-call API.
  - Added LZMA variant attempts (`props-only-header`, `props+size-header`) with strict output-size validation.
  - Updated block0 mode candidate ordering to reduce over-bias toward header/block flag and include fail-hit demotion.
  - Updated reconstruction candidate window generation to coarse + fine passes (reduced noisy search explosion).
  - Reweighted reconstruction scoring (decoded-block ratio + continuity + node-range pass weight).
  - Split reconstruction failure classification (`SEEK`, `READ`, `RANGE`, `LZ4`, `LZMA`, `MODE_UNSUPPORTED`, etc.).
  - Expanded raw serialized fallback scan stride/window to improve candidate discovery.

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added additive probe fields:
    - `lzma_decode_attempted`
    - `lzma_decode_variant`
    - `block0_mode_rank`
    - `recon_failure_detail_code`

- `tools/vsfavatar_sidecar.cpp`
  - Emitted new optional schema v3 additive fields above.
  - Added matching warning lines (`W_LZMA`, `W_RECON_DETAIL`).

- `src/avatar/vsfavatar_loader.cpp`
  - Consumed new optional sidecar fields as warnings only (no contract break).
  - Extended in-house metadata warning summary with new diagnostics.

- `CMakeLists.txt`
  - Enabled C language for build.
  - Added `third_party/LzmaDec.c` to `vsfclone_core` sources.
  - Added `third_party` include path for LZMA headers.

- `third_party/LzmaDec.c` (new)
- `third_party/LzmaDec.h` (new)
- `third_party/Types.h` (new)
  - Imported public-domain LZMA decoder components (Igor Pavlov) used by UnityFS mode=1 decode path.

### Verification

- `tools/vsfavatar_quality_gate.ps1 -UseFixedSet`
  - GateA: PASS
  - GateB: PASS
  - GateC: PASS
  - GateD: PASS
  - Overall: PASS
- `tools/vrm_quality_gate.ps1`
  - Overall: PASS
- `tools/vxavatar_quality_gate.ps1`
  - FAIL due to missing fixed-valid VX/VXA2 samples in current quick profile dataset (not a parser regression from this change set).

## 2026-03-03 - Host render advanced controls and local preset persistence

### Summary

Expanded host render UX with manual composition controls and reusable local presets, keeping WPF and WinUI behavior aligned.

### Changed

- `host/HostCore/HostInterfaces.cs`
  - Added `IRenderPresetStore` interface.

- `host/HostCore/RenderPresetStore.cs` (new)
  - Added preset model:
    - `RenderPresetModel`
    - `RenderPresetStoreModel`
  - Added local JSON persistence implementation:
    - `RenderPresetStore`
  - Added model normalization, value clamping, duplicate-name collapse, and corrupt-file fallback with `.bak` backup.

- `host/HostCore/HostController.cs`
  - Added preset management API:
    - `CreatePreset`
    - `SaveOrUpdateRenderPreset`
    - `ApplyRenderPreset`
    - `DeleteRenderPreset`
    - `ResetRenderDefaults`
  - Added exposed preset state:
    - `RenderPresets`
    - `SelectedRenderPresetName`
  - Updated broadcast toggle behavior to preserve user camera controls (`CameraMode`, `Framing`, `Headroom`, `Yaw`, `FOV`, `Mirror`, overlay flag) while switching preset baseline.
  - Added render state normalization/clamping path shared by UI apply and preset apply.

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
  - Added advanced render controls:
    - `Camera Mode`, `Headroom`, `Yaw`, `FOV`, `Mirror`
  - Added preset controls:
    - save/apply/delete/reset UI and handlers
  - Added render apply debounce timer (`~100ms`) to reduce high-frequency native apply calls during slider drag.

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`
  - Added the same advanced render/preset controls and behavior as WPF.
  - Added matching debounce and diagnostics field expansion for parity.

- `docs/reports/ui_render_benchmark_plan_2026-03-02.md`
  - Added advanced-controls implementation update and KPI status refinement.
- `docs/reports/ui_render_advanced_controls_2026-03-03.md` (new)
  - Added detailed implementation summary, behavior notes, and verification snapshot for advanced controls + preset persistence.

## 2026-03-03 - XAV2 Unity SDK diagnostics API + VRM-derived fixed sample generation

### Summary

Strengthened Unity-side XAV2 SDK loader reliability with a non-throwing diagnostics API, added stricter section boundary/schema validation while preserving v1 compatibility, and expanded gate input stability by generating fixed-valid XAV2 samples directly from VRM assets.

### Changed

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
  - Added load diagnostics contracts:
    - `Xav2LoadErrorCode`
    - `Xav2LoadDiagnostics` (`ErrorCode`, `ErrorMessage`, `ParserStage`, `IsPartial`, `Warnings`)
  - Updated manifest default exporter version to `0.3.0`.

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`
  - Added non-throwing API:
    - `TryLoad(path, out payload, out diagnostics)`
  - Preserved existing API:
    - `Load(path)` now wraps `TryLoad` and throws with diagnostic context on failure.
  - Added boundary/schema validation for manifest and TLV section payloads.
  - Kept backward compatibility for material payloads with/without `shaderVariant`.
  - Added unknown-section and partial-compat diagnostics warnings.

- `unity/Packages/com.vsfclone.xav2/Editor/Xav2Exporter.cs`
  - Centralized manifest defaults (`schemaVersion=1`, `exporterVersion=0.3.0`).
  - Ensured required manifest fields and ref arrays are always populated.
  - Standardized strict shader policy failure message format with material/shader identifiers.

- `tools/vxavatar_sample_report.ps1`
  - Added `-FixedXav2FromVrmCount` (default `5`).
  - Added VRM-driven fixed XAV2 generation path (`vrm_to_xav2`) that seeds `fixed-valid` XAV2 rows from `.vrm` inputs.

- `tools/vxavatar_quality_gate.ps1`
  - Added `-FixedXav2FromVrmCount` pass-through to sample report generation.

- `README.md`
- `unity/Packages/com.vsfclone.xav2/README.md`
  - Documented `TryLoad` + diagnostics contract and fixed XAV2 sample generation policy.

## 2026-03-03 - XAV2 TryLoad strict option + runtime tests + deterministic VRM allowlist gate

### Summary

Completed the follow-up hardening slice for XAV2 SDK by adding an option-based strict validation path, introducing Unity runtime loader tests, and making fixed-valid XAV2 generation deterministic through a VRM allowlist policy.

### Changed

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
  - Added:
    - `Xav2LoadOptions` (`StrictValidation`)
    - `Xav2LoadErrorCode.StrictValidationFailed`

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2RuntimeLoader.cs`
  - Added overload:
    - `TryLoad(path, out payload, out diagnostics, options)`
  - Kept compatibility:
    - existing `Load(...)` and `TryLoad(...)` signatures remain, now delegating to default options.
  - Added strict-mode behavior:
    - warning-level conditions now fail when `StrictValidation=true`:
      - unknown sections
      - trailing bytes in section payload
      - ref/payload mismatch diagnostics

- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/VsfClone.Xav2.Runtime.Tests.asmdef` (new)
- `unity/Packages/com.vsfclone.xav2/Tests/Runtime/Xav2RuntimeLoaderTests.cs` (new)
  - Added runtime test coverage for:
    - valid load
    - magic/version/manifest/section truncation failures
    - legacy material compatibility (without `shaderVariant`)
    - strict/non-strict unknown-section behavior
    - strict ref/payload mismatch failure

- `tools/vxavatar_sample_report.ps1`
- `tools/vxavatar_quality_gate.ps1`
  - Added deterministic allowlist-first VRM generation contract:
    - `-FixedXav2FromVrmAllowlist`
    - `-FixedXav2FromVrmCount`
  - In fixed/full modes, missing allowlist entries now fail input preparation.

- `README.md`
- `unity/Packages/com.vsfclone.xav2/README.md`
  - Documented strict option path, test location, and deterministic gate input policy.

## 2026-03-03 - Host render UI controls sync finalization + benchmark plan

### Summary

Finalized host-side render option controls for both WPF and WinUI by wiring the same Render UI state flow through HostCore and documenting KPI-based follow-up validation scenarios.

### Changed

- `host/HostCore/HostController.cs`
  - Added render option apply/sync helpers:
    - native apply/readback (`nc_set_render_quality_options` / `nc_get_render_quality_options`)
    - camera mode mapping between host and native enums
    - background preset encode/decode helpers
    - centralized `RenderUiState` reconstruction from applied native options

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
  - Added Render control panel:
    - `Broadcast Mode`
    - `Framing` slider with live numeric label
    - `Background` preset combo (`Dark Blue`, `Neutral Gray`, `Green Screen`)
    - `Show Debug Overlay`
  - Added bidirectional UI/state sync and reentry guard (`_isSyncingRenderUi`).
  - Added on-canvas debug overlay panel that mirrors runtime diagnostics content.

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`
  - Added the same Render control surface and debug overlay behavior as WPF.
  - Added matching UI/state sync flow and event-driven apply path.

- `docs/reports/ui_render_benchmark_plan_2026-03-02.md` (new)
  - Added KPI checklist and runtime acceptance scenarios for framing, visibility, overlay hygiene, background presets, DPI stability, and frame-time regression checks.

- `docs/INDEX.md`
  - Added index entry for the render benchmark plan report.

### Verified

- Build success:
  - `dotnet build host/HostCore/HostCore.csproj -c Release`
- Environment-constrained build result:
  - WPF/WinUI project restore failed in current environment due to `NU1301` (`api.nuget.org:443` unreachable), so host-app compilation must be revalidated in network-enabled CI/local setup.

## 2026-03-02 - NativeCore render quality API contract sync

### Summary

Exposed render quality controls in the public native C header so the ABI now matches existing runtime capabilities.

### Changed

- `include/vsfclone/nativecore/api.h`
  - Added public enum:
    - `NcCameraMode`
      - `NC_CAMERA_MODE_AUTO_FIT_FULL`
      - `NC_CAMERA_MODE_AUTO_FIT_BUST`
      - `NC_CAMERA_MODE_MANUAL`
  - Added public options struct:
    - `NcRenderQualityOptions`
      - framing/camera/background/overlay controls
  - Added API declarations:
    - `nc_set_render_quality_options`
    - `nc_get_render_quality_options`

- `docs/reports/nativecore_render_quality_api_sync_2026-03-02.md` (new)
  - Added detailed contract sync notes and compatibility impact summary.

## 2026-03-03 - XAV2 payload expansion + signature dispatch fallback + VSFAvatar probe hardening

### Summary

Expanded XAV2 runtime payload coverage (skin/blendshape), added extension-independent loader dispatch fallback via file signature probing, and hardened VSFAvatar serialized probing with candidate/window/raw-bundle fallback paths and complete-state sidecar normalization.

### Changed

- `include/vsfclone/avatar/i_avatar_loader.h`
  - Added `CanLoadBytes(...)` interface for header-based loader routing.

- `src/avatar/avatar_loader_facade.cpp`
  - Added head-byte reader and signature fallback dispatch path when extension dispatch misses.

- `src/avatar/xav2_loader.cpp`
  - Added decode support for:
    - `0x0013` skin payload sections
    - `0x0014` blendshape payload sections
  - Added mesh-key linkage for skin/blendshape runtime payload attachment.
  - Added material override parser compatibility path with default variant fallback.
  - Added partial mapping warnings for mesh-ref and payload section cardinality mismatch.

- `include/vsfclone/avatar/avatar_package.h`
  - Expanded payload model fields used by XAV2 skin/blendshape decode paths.

- `src/vsf/unityfs_reader.cpp`
  - Expanded serialized candidate discovery with:
    - truncated node-window handling
    - all-node fallback
    - in-stream serialized-header scan fallback
    - wider offset deltas
  - Added raw-bundle serialized scan fallback path for failed node-based probe cases.

- `src/vsf/serialized_file_reader.cpp`
  - Added offset-scan parse fallback for misaligned serialized byte windows.

- `tools/vsfavatar_sidecar.cpp`
  - Normalized complete-state contract:
    - `probe_stage=complete && object_table_parsed=true` emits `primary_error_code=NONE`
  - Refined complete-state compatibility/missing-feature labeling.

- `tools/vxavatar_sample_report.ps1`
- `tools/vxavatar_quality_gate.ps1`
- `.github/workflows/vxavatar-gate.yml`
  - Expanded quality-gate/report/workflow scope from VXAvatar/VXA2 to VXAvatar/VXA2/XAV2.
  - Added XAV2 fixed-valid and synthetic-corrupt gate contracts (Gate F / Gate G).

- `unity/Packages/com.vsfclone.xav2/Runtime/Xav2DataModel.cs`
  - Added schema/exporter metadata and runtime data containers for skin/blendshape payloads.

### Verified

- Release build success:
  - `cmake --build build --config Release`
- VXAvatar/VXA2/XAV2 quick gate success:
  - `powershell -ExecutionPolicy Bypass -File .\tools\vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`
  - `GateA/B/C/D/E/F/G=PASS`
- Signature fallback behavior check success:
  - renamed a `.vxa2` sample to `.bin`, `avatar_tool` still detected `Format: VXA2` via header signature dispatch.

## 2026-03-03 - Host auto-quality pass (DPI-aware render sizing + resize debounce + Spout auto reconfigure)

### Summary

Addressed perceived blurriness/pixel-break artifacts in host preview by introducing DPI-aware physical-pixel render sizing and automatic runtime reconfiguration behavior.  
Applied the same logic to both WPF and WinUI hosts without exposing manual quality toggles.

### Changed

- `host/HostCore/HostUiState.cs`
  - Extended `HostSessionState` with render-metric fields:
    - `LogicalWidth`
    - `LogicalHeight`
    - `DpiScaleX`
    - `DpiScaleY`
    - `RenderWidthPx`
    - `RenderHeightPx`
  - Extended `OutputState` with Spout runtime dimensions:
    - `SpoutWidthPx`
    - `SpoutHeightPx`
    - `SpoutFps`

- `host/HostCore/HostController.cs`
  - Added `UpdateRenderMetrics(...)` to publish effective render sizing telemetry.
  - `ResizeWindow(...)` now triggers auto Spout reconfiguration when active and target size changes.
  - Added automatic Spout stop/start flow on render target resize:
    - `SpoutAutoStop`
    - `SpoutAutoStart`
    - `SpoutAutoReconfigure` log entry
  - Preserved existing interface surface while improving runtime behavior observability.

- `host/WpfHost/MainWindow.xaml.cs`
  - Added DPI-aware pixel-size computation using `VisualTreeHelper.GetDpi(RenderHost)`.
  - Switched attach/resize/Spout-start dimensions from logical size to physical pixel size.
  - Added resize debounce timer (`~90ms`) to reduce resize-thrash and avoid repeated swapchain churn.
  - Runtime diagnostics now include auto-quality line:
    - logical size
    - DPI scale
    - effective render target pixel size

- `host/WinUiHost/MainWindow.xaml.cs`
  - Added DPI-aware pixel-size computation using `RenderHost.XamlRoot.RasterizationScale`.
  - Switched attach/resize/Spout-start dimensions to physical pixel size parity with WPF.
  - Added resize debounce timer (`~90ms`) with matching behavior.
  - Runtime diagnostics now include the same auto-quality telemetry line.

### Verified

- Build success:
  - `dotnet build host/WpfHost/WpfHost.csproj -c Release`
- Build attempt failed in current environment/toolchain:
  - `dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64`
  - failure point remains Windows App SDK XAML compiler (`XamlCompiler.exe` exit code 1).

## 2026-03-02 - XAV2 container path + VRM to XAV2 converter scaffold

### Summary

Added a first-class `.xav2` avatar container path based on the existing TLV family, plus a converter utility that packages VRM runtime payloads into XAV2.

### Changed

- `src/avatar/xav2_loader.cpp` / `src/avatar/xav2_loader.h` (new)
  - Added `.xav2` loader with:
    - `XAV2` magic/version validation
    - manifest required key checks (`avatarId`, `meshRefs`, `materialRefs`, `textureRefs`)
    - TLV decode for:
      - `0x0011` mesh render payload
      - `0x0002` texture payload
      - `0x0003` material override
      - `0x0012` material shader params JSON
    - compatibility/error signaling (`XAV2_SCHEMA_INVALID`, `XAV2_SECTION_TRUNCATED`, `XAV2_ASSET_MISSING`)

- `src/avatar/avatar_loader_facade.cpp`
  - Registered `.xav2` dispatch route.

- `include/vsfclone/avatar/avatar_package.h`
  - Added `AvatarSourceType::Xav2`.
  - Added `MaterialRenderPayload.shader_params_json`.

- `include/vsfclone/nativecore/api.h`
  - Added `NC_AVATAR_FORMAT_XAV2`.

- `src/nativecore/native_core.cpp`
  - Added native format hint mapping for `AvatarSourceType::Xav2`.

- `host/HostCore/NativeCoreInterop.cs`
  - Added managed enum mapping `NcAvatarFormatHint.Xav2`.

- `tools/vrm_to_xav2.cpp` (new)
  - Added converter utility:
    - input: `.vrm`
    - output: `.xav2`
    - writes mesh/material/texture payload sections from runtime extraction

- `CMakeLists.txt`
  - Added `xav2_loader.cpp` to `vsfclone_core`.
  - Added `vrm_to_xav2` executable target.

- `docs/formats/xav2.md` (new)
  - Added XAV2 format draft and section contract.

- `README.md`, `docs/INDEX.md`
  - Updated format and tool references to include `.xav2` and `vrm_to_xav2`.

## 2026-03-03 - Host UI operation-focused redesign (WPF + WinUI) and shared state controller

### Summary

Reworked both host shells from MVP button panels into operation-focused UI layouts with explicit lifecycle sections, status strip visibility, structured diagnostics, and guarded actions.  
Added a shared HostCore controller/state model so WPF and WinUI follow the same runtime workflow and control semantics.

### Changed

- `host/HostCore/HostInterfaces.cs` (new)
  - Added explicit host service contracts:
    - `IAvatarSessionService`
    - `IRenderLoopService`
    - `IOutputService`
  - Enables host-shell behavior to depend on interfaces instead of concrete service classes.

- `host/HostCore/HostUiState.cs` (new)
  - Added UI-facing state contracts:
    - `HostSessionState`
    - `OutputState`
    - `DiagnosticsSnapshot`
    - `HostLogEntry`
  - Defines a stable snapshot/log model for both host tracks.

- `host/HostCore/HostController.cs` (new)
  - Added a shared orchestration layer over session/render/output services.
  - Added bounded operation log ring buffer (200 entries).
  - Added events:
    - `StateChanged`
    - `DiagnosticsUpdated`
    - `ErrorRaised`
  - Added guarded workflow methods:
    - initialize/shutdown
    - attach/resize/tick
    - load/unload avatar
    - start/stop Spout
    - start/stop OSC
  - Added unified diagnostics snapshot publication on each state transition/tick.

- `host/HostCore/AvatarSessionService.cs`
- `host/HostCore/RenderLoopService.cs`
- `host/HostCore/OutputService.cs`
  - Updated services to implement new interface contracts.

- `host/WpfHost/MainWindow.xaml`
- `host/WpfHost/MainWindow.xaml.cs`
  - Replaced MVP layout with operation-focused sections:
    - `Session` (Initialize/Shutdown)
    - `Avatar` (path + Browse + Load/Unload)
    - `Outputs` (Spout/OSC config + start/stop)
  - Added structured diagnostics tabs:
    - Runtime
    - Avatar
    - Logs
  - Added status strip:
    - session/avatar/render/frame/output/last-error
  - Added state-based button enable/disable rules.
  - Added guarded confirmations for disruptive actions (reinitialize, unload, stop outputs, shutdown).
  - Added input validation for OSC bind port.
  - Added file picker flow for avatar selection.

- `host/WinUiHost/MainWindow.xaml`
- `host/WinUiHost/MainWindow.xaml.cs`
  - Applied the same operation-focused information architecture and interaction semantics as WPF.
  - Added runtime/avatar/log diagnostics panels and status strip parity.
  - Added WinUI content dialog confirmations and file picker avatar browse flow.
  - Added render-host resize handling to align with WPF runtime behavior.

### Verified

- Build success:
  - `dotnet build host/WpfHost/WpfHost.csproj -c Release`
- Build attempt failed in current environment/toolchain:
  - `dotnet build host/WinUiHost/WinUiHost.csproj -c Release -p:Platform=x64`
  - failure point: Windows App SDK XAML compiler (`XamlCompiler.exe` exit code 1) with no surfaced line-level diagnostics in generated `output.json`.

## 2026-03-03 - VSFAvatar serialized candidate fallback expansion and complete-state normalization

### Summary

Improved VSFAvatar serialized probing resilience by adding node-window and stream-scan fallback candidate discovery, and normalized sidecar complete-state reporting so `probe_stage=complete` consistently emits `primary_error_code=NONE`.

### Changed

- `src/vsf/unityfs_reader.cpp`
  - Expanded serialized candidate selection:
    - accept truncated node windows when reconstructed stream is partial
    - fallback to all-node candidate probing when CAB/assets paths are unusable
    - fallback to bounded stream scan for SerializedFile-like headers when node candidates are empty
  - Extended candidate offset deltas to improve alignment recovery (`±128`, `±256`).
  - Added richer candidate scoring/sorting and normalized empty-candidate failure code (`SF_NO_CANDIDATE_WINDOW`).

- `src/vsf/serialized_file_reader.cpp`
  - Added bounded offset scan fallback for misaligned serialized payloads.
  - Reuses existing parse path while annotating successful scan origin (`+scan@<offset>`).

- `tools/vsfavatar_sidecar.cpp`
  - Normalized complete-state primary error:
    - `probe_stage=complete && object_table_parsed=true` => `primary_error_code=NONE`
  - Updated compatibility classification to treat complete/object-table-parsed as `full`.
  - Refined missing-feature labeling for object-table parsed but mesh-zero cases.

## 2026-03-03 - VRM runtime draw-path activation + host diagnostics/publish hardening

### Summary

Upgraded VRM runtime rendering from clear-only validation to an actual D3D11 mesh/material draw path, fixed HostCore interop drift for new avatar diagnostics fields, and hardened host publish defaults for practical EXE delivery on locked-file environments.

### Changed

- `src/nativecore/native_core.cpp`
  - Added D3D11 pipeline resources (VS/PS/input layout/constant buffer/depth/blend/sampler) and per-avatar GPU resource caches.
  - Added mesh/index GPU upload and material SRV binding path.
  - Added WIC-based texture decode/upload for VRM texture payloads.
  - Added real frame draw-call counting and storage in `AvatarPackage.last_render_draw_calls`.
  - Added runtime camera/world fit defaults for immediate on-screen avatar visibility.
  - Added renderer resource cleanup on unload/destroy/shutdown to avoid stale GPU handles.

- `include/vsfclone/avatar/avatar_package.h`
  - Extended mesh payload metadata:
    - `vertex_stride`
    - `material_index`
  - Extended material payload metadata:
    - `alpha_mode`
    - `alpha_cutoff`
    - `double_sided`

- `src/avatar/vrm_loader.cpp`
  - Added `TEXCOORD_0` extraction and interleaved position/uv vertex payload generation.
  - Added primitive-level material index mapping in mesh payloads.
  - Propagated parsed material alpha/double-sided properties into runtime payloads.

- `host/HostCore/NativeCoreInterop.cs`
  - Aligned managed `NcAvatarInfo` with native struct tail fields:
    - `ExpressionCount`
    - `LastRenderDrawCalls`
    - `LastExpressionSummary`
  - Added `nc_get_avatar_info` P/Invoke entry.

- `host/HostCore/AvatarSessionService.cs`
  - Added `RefreshAvatarInfo()` to re-query live avatar diagnostics each frame.

- `host/WpfHost/MainWindow.xaml.cs`
  - Added render-loop result capture and live diagnostics output:
    - `RenderRc`
    - `DrawCalls`
    - `Expressions`
    - `ExpressionSummary`

- `tools/publish_hosts.ps1`
  - Switched to WPF-first default publish flow.
  - Added optional WinUI publish switch: `-IncludeWinUi`.
  - Added running-host process stop step before publish.
  - Added native build fallback path (`build_hotfix`) for locked `nativecore.dll` cases.

- `host/WinUiHost/WinUiHost.csproj`
  - Added publish compatibility settings used in current toolchain:
    - `EnableMsixTooling=true`
    - `Platforms/Platform/PlatformTarget=x64`
    - `UseRidGraph=true`

- `CMakeLists.txt`
  - Added Windows nativecore link dependencies required by the upgraded renderer/texture path:
    - `d3dcompiler`
    - `ole32`
    - `windowscodecs`

### Verified

- `nativecore` Release target builds successfully after renderer/pipeline changes.
- Patched `nativecore.dll` was deployed to:
  - `dist/wpf/nativecore.dll`
  - `dist/wpf_full/nativecore.dll`
- WPF host diagnostics now expose draw/render telemetry required to distinguish:
  - "avatar loaded but not rendered"
  - "draw calls executed but camera/material issue"

## 2026-03-03 - Host publish CI smoke workflow and artifact contract checks

### Summary

Added a dedicated Windows CI workflow for host publish so WPF/WinUI distribution outputs are validated automatically on host-related changes.

### Changed

- `.github/workflows/host-publish.yml` (new)
  - Added trigger paths for host/nativecore/publish-script changes.
  - Added Windows CI job (`publish-hosts`) with:
    - CMake configure (`VS 2022`, `x64`)
    - Release native build
    - host publish script execution (`tools/publish_hosts.ps1 -SkipNativeBuild`)
    - required artifact assertions for both host tracks and publish report
    - artifact upload bundle (`host-publish-outputs`)

- `docs/reports/host_exe_publish_2026-03-02.md`
  - Added CI smoke validation section and artifact assertion contract.
  - Updated next-step list by removing already-implemented CI item.

### Verified

- CI workflow definition exists at:
  - `NativeVsfClone/.github/workflows/host-publish.yml`
- Workflow validates required deliverables:
  - `dist/wpf/WpfHost.exe`
  - `dist/wpf/nativecore.dll`
  - `dist/winui/WinUiHost.exe`
  - `dist/winui/nativecore.dll`
  - `build/reports/host_publish_latest.txt`

## 2026-03-03 - Host publish documentation refresh and index normalization

### Summary

Refined host publish documentation to be decision-complete for operation and maintenance, and normalized docs index coverage so host-track reports are discoverable from a single entrypoint.

### Changed

- `docs/reports/host_exe_publish_2026-03-02.md`
  - Reorganized report using documentation template sections:
    - `Scope`
    - `Implemented Changes`
    - `Verification Summary`
    - `Known Limitations`
    - `Next Steps`
  - Added explicit script parameter contract (`-Configuration`, `-RuntimeIdentifier`, `-SkipNativeBuild`).
  - Added step-by-step publish behavior and output contract details.
  - Added operational constraints and follow-up recommendations.

- `docs/INDEX.md`
  - Added missing host-track report entries:
    - `ui_host_runtime_integration_2026-03-02.md`
    - `host_exe_publish_2026-03-02.md`

### Verified

- `docs/INDEX.md` now directly links all host-track reports created on `2026-03-02`.
- Host publish report contains executable run commands, required outputs, and failure/limitation context in one place.

## 2026-03-02 - Host EXE publish pipeline (WPF + WinUI)

### Summary

Added reproducible GUI host publish automation so both WPF and WinUI hosts can be produced as self-contained executables and distributed with `nativecore.dll`.

### Changed

- `host/WpfHost/WpfHost.csproj`
  - Added publish defaults:
    - `RuntimeIdentifier=win-x64`
    - `SelfContained=true`
    - `PublishSingleFile=true`
    - `PublishTrimmed=false`

- `host/WinUiHost/WinUiHost.csproj`
  - Added publish defaults:
    - `RuntimeIdentifier=win-x64`
    - `SelfContained=true`
    - `PublishSingleFile=true`
    - `PublishTrimmed=false`
    - `WindowsAppSDKSelfContained=true`

- `tools/publish_hosts.ps1`
  - Added end-to-end publish script:
    - native `Release` build
    - WPF + WinUI publish
    - `nativecore.dll` copy to both outputs
    - consolidated output in `dist/wpf`, `dist/winui`
    - report output in `build/reports/host_publish_latest.txt`

- `docs/reports/host_exe_publish_2026-03-02.md`
  - Added operational publish report and expected output contract.

### Verified

- Host publish command path documented:
  - `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1`
- Distribution contract documented:
  - `dist/wpf/*.exe + nativecore.dll`
  - `dist/winui/*.exe + nativecore.dll`

## 2026-03-02 - UI host foundation + native window render path + runtime Spout/OSC backends

### Summary

Implemented the UI integration foundation by adding shared HostCore + WPF/WinUI host projects, and expanded `nativecore.dll` with a window-bound render path and runtime output backends for Spout/OSC workflows.

### Changed

- `include/vsfclone/nativecore/api.h`
  - Added window-render API:
    - `nc_create_window_render_target`
    - `nc_resize_window_render_target`
    - `nc_destroy_window_render_target`
    - `nc_render_frame_to_window`
  - Added runtime stats API:
    - `NcRuntimeStats`
    - `nc_get_runtime_stats`

- `src/nativecore/native_core.cpp`
  - Replaced stub backend wiring with runtime classes:
    - `stream::SpoutSender`
    - `osc::OscEndpoint`
  - Added D3D11 swapchain-per-window lifecycle management.
  - Added shared render pipeline path used by both external-context and window-target rendering.
  - Added render-time output hooks:
    - frame capture from RTV to streaming backend
    - tracking frame publish to OSC addresses (`/VsfClone/Tracking/*`)
  - Added runtime diagnostics updates (`last_frame_ms`, spout/osc active status).

- `include/vsfclone/stream/spout_sender.h`
- `src/stream/spout_sender.cpp`
  - Added shared-memory based BGRA frame sender implementation (channel-scoped mapped file).

- `include/vsfclone/osc/osc_endpoint.h`
- `src/osc/osc_endpoint.cpp`
  - Added Winsock UDP OSC endpoint implementation with float packet encoding and destination parsing.

- `CMakeLists.txt`
  - Switched core sources from stub files to runtime files.
  - Linked nativecore against `d3d11`, `dxgi`, `ws2_32` on Windows.

- `host/HostCore/*`
  - Added .NET interop and services:
    - `NativeCoreInterop`
    - `AvatarSessionService`
    - `RenderLoopService`
    - `OutputService`
    - `DiagnosticsModel`

- `host/WpfHost/*`
  - Added runnable WPF host shell for:
    - initialize/load/unload
    - render tick loop
    - spout/osc start-stop
    - runtime diagnostics panel

- `host/WinUiHost/*`
  - Added WinUI host shell with parity controls and diagnostics over shared HostCore.

- `host/HostApps.sln`
  - Added host-focused solution file grouping HostCore/WpfHost/WinUiHost.

## 2026-03-03 - VRM expression wiring + fixed/auto gate profiles + render diagnostics

### Summary

Advanced the VRM quality track by adding expression extraction/runtime mapping, exposing expression/render diagnostics through NativeCore, and splitting VRM quality gate execution into deterministic `fixed5` and exploratory `auto5` profiles.

### Changed

- `include/vsfclone/avatar/avatar_package.h`
  - Added expression/runtime state container:
    - `ExpressionState`
    - `AvatarPackage.expressions`
    - `AvatarPackage.last_expression_summary`
    - `AvatarPackage.last_render_draw_calls`

- `include/vsfclone/nativecore/api.h`
  - Extended `NcAvatarInfo` tail fields:
    - `expression_count`
    - `last_render_draw_calls`
    - `last_expression_summary`

- `src/nativecore/native_core.cpp`
  - `nc_set_tracking_frame` now applies minimal expression runtime mapping for extracted expressions:
    - `blink` -> average blink
    - `viseme_aa` -> `mouth_open`
    - `joy` -> mouth-driven proxy weight
  - `nc_render_frame` now tracks and stores per-avatar draw-call counts (mesh payload count proxy).
  - `FillAvatarInfo` now surfaces expression/render diagnostics into `NcAvatarInfo`.

- `tools/avatar_tool.cpp`
  - Prints new diagnostics:
    - `ExpressionCount`
    - `LastRenderDrawCalls`
    - `LastExpressionSummary`

- `src/avatar/vrm_loader.cpp`
  - Added VRM expression extraction:
    - `extensions.VRMC_vrm.expressions.preset/custom`
    - legacy `extensions.VRM.blendShapeMaster.blendShapeGroups`
  - Added expression mapping tags (`blink`, `viseme_aa`, `joy`, `none`).
  - Added warnings/feature flags for expression fallback visibility.

- `tools/vrm_quality_gate.ps1`
  - Added gate profiles:
    - `-Profile fixed5` (default)
    - `-Profile auto5`
  - Added Gate D (`ExpressionCount > 0` per sample).
  - Profile-specific outputs:
    - `fixed5`: `vrm_probe_fixed5.txt`, `vrm_gate_fixed5.txt`
    - `auto5`: `vrm_probe_auto5.txt`, `vrm_gate_auto5.txt`

- `tools/vsfavatar_quality_gate.ps1`
  - Added per-sample Gate D unmet diagnostics (`stage/object_table_parsed/primary`) to failure reasons.

- `README.md`
  - Updated VRM gate section for profile-based execution and Gate D expression visibility.

## 2026-03-03 - VRM material/texture payload quality pass + 5-sample gate harness

### Summary

Started the VRM quality sprint by replacing default-only VRM material scaffolding with parsed material/texture payload extraction and adding a strict 5-sample quality gate harness.

### Changed

- `src/avatar/vrm_loader.cpp`
  - Added JSON helpers:
    - `TryGetBool(...)`
    - texture format inference (`DetectTextureFormat`)
  - Added GLB image extraction from BIN `bufferView` ranges.
  - Added `materials[]` parse with basic fields:
    - `name`
    - `doubleSided`
    - `alphaMode`
    - `alphaCutoff`
    - `pbrMetallicRoughness.baseColorTexture.index`
  - Added `textures[] -> images[]` source resolution.
  - Populates:
    - `materials`
    - `material_payloads`
    - `texture_payloads`
  - Added diagnostics and quality downgrade behavior:
    - `VRM_TEXTURE_MISSING`
    - `VRM_MATERIAL_UNSUPPORTED`
    - unresolved/unsupported paths now return `Compat=partial` (when mesh path is otherwise valid).

- `tools/vrm_quality_gate.ps1` (new)
  - Added strict VRM 5-sample quality gate harness.
  - Runs `avatar_tool` per sample and evaluates:
    - Gate A: load stability
    - Gate B: VRM/runtime-ready/mesh payload contract
    - Gate C: material+texture payload minimum
  - Outputs:
    - `build/reports/vrm_probe_latest.txt`
    - `build/reports/vrm_gate_summary.txt`
  - Exit code:
    - `0` pass, `1` fail

- `README.md`
  - Added `VRM quality gate` section (command, gate definitions, outputs, exit-code policy).

## 2026-03-03 - VRM minimal runtime-ready slice + native render clear path

### Summary

Implemented the first functional VRM vertical slice from file parse to runtime payload readiness, and upgraded native render path from pure placeholder validation to minimal D3D11 render execution (`ClearRenderTargetView`).

### Changed

- `src/avatar/vrm_loader.cpp`
  - Replaced scaffold-only loader with minimal GLB v2 parser flow:
    - header + chunk validation (`JSON`, `BIN`)
    - in-loader lightweight JSON parse
    - accessor/bufferView decode for:
      - `POSITION` (`FLOAT VEC3`)
      - `indices` (`U8/U16/U32`, fallback sequential indices)
  - Added payload population:
    - `mesh_payloads`
    - `materials` / `material_payloads` (minimal placeholder)
  - Added parser stage + error code contract:
    - stages: `parse -> resolve -> payload -> runtime-ready`
    - errors: `VRM_SCHEMA_INVALID`, `VRM_ASSET_MISSING`, `NONE`

- `src/nativecore/native_core.cpp`
  - `nc_create_render_resources` now rejects avatars with no mesh payloads.
  - `nc_render_frame` now executes minimal D3D11 frame action:
    - bind RTV
    - clear RTV with a fixed color
  - Added `NOMINMAX` guard for Windows macro conflicts with STL.

- `CMakeLists.txt`
  - Linked `nativecore` against `d3d11` on Windows.

### Verified

- Configure/build:
  - `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`
  - `cmake --build build --config Release`
- VRM sample runtime checks via `avatar_tool`:
  - `sample/개인작08.vrm`
    - `Format=VRM`
    - `Compat=full`
    - `ParserStage=runtime-ready`
    - `MeshPayloads=9`
  - `sample/Kikyo_FT Variant.vrm`
    - `Format=VRM`
    - `Compat=full`
    - `ParserStage=runtime-ready`
    - `MeshPayloads=22`

## 2026-03-03 - VXAvatar/VXA2 gate profiles + CI strict enforcement

### Summary

Expanded the VXAvatar/VXA2 quality gate from fixed local checks to profile-based strict enforcement (`quick`/`full`) and added CI workflow integration with machine-readable gate output.

### Changed

- `tools/vxavatar_sample_report.ps1`
  - Added profile control:
    - `-Profile quick|full`
  - `quick`: fixed baseline + synthetic corruption samples.
  - `full`: fixed baseline + discovered real samples + synthetic corruption samples.
  - Added report header field:
    - `Profile`
  - Gate input marker bumped:
    - `GateInputVersion: 2`

- `tools/vxavatar_quality_gate.ps1`
  - Added profile control:
    - `-Profile quick|full`
  - Added JSON summary output:
    - `build/reports/vxavatar_gate_summary.json`
  - Added Gate E for full-profile real-sample coverage.
  - Added `-RequireRealFullSamples` option for strict full-profile environments.
  - Strict pass policy remains exit-code based (`0` pass, `1` fail).

- `.github/workflows/vxavatar-gate.yml` (new)
  - Added `quick-gate` job:
    - build + `vxavatar_quality_gate.ps1 -UseFixedSet -Profile quick`
  - Added `full-gate` job:
    - build + `vxavatar_quality_gate.ps1 -Profile full`
  - Added artifact upload for probe/summary outputs.

- `README.md`
  - Updated VXAvatar/VXA2 gate usage for quick/full profile commands.
  - Added CI gate behavior and JSON summary output documentation.

- `docs/INDEX.md`
  - Added report link:
    - `docs/reports/vxavatar_gate_ci_expansion_2026-03-03.md`

- `docs/reports/vxavatar_gate_ci_expansion_2026-03-03.md` (new)
  - Added implementation and CI rollout details for profile-based strict gating.

## 2026-03-03 - VSFAvatar serialized-candidate expansion + strict GateD

### Summary

Expanded VSFAvatar serialized candidate probing with bounded offset deltas, added serialized-candidate diagnostics to sidecar/loader output, and tightened fixed-set gate strictness with a new Gate D milestone (`complete + object_table_parsed + no primary error`).

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added serialized probe observability fields:
    - `serialized_attempt_count`
    - `serialized_best_candidate_path`
    - `serialized_best_candidate_score`

- `src/vsf/unityfs_reader.cpp`
  - Extended serialized candidate parsing attempts with offset deltas:
    - `0`, `+16`, `-16`, `+32`, `-32`, `+64`, `-64`
  - Added score policy for candidate selection:
    - prioritize parsed `object_count`
    - tie-break using major-type diversity (`GameObject`, `Mesh`, `Material`, `Texture2D`, `SkinnedMeshRenderer`)
  - Preserves highest-scored failure code/path when all candidates fail.

- `src/vsf/serialized_file_reader.cpp`
  - Added parse-error classification in final dual-endian failure string:
    - `SF_PARSE_BOTH_ENDIAN_FAILED[<little_code>|<big_code>]`
  - Normalized success summary error code to `NONE`.

- `tools/vsfavatar_sidecar.cpp`
  - Added serialized diagnostics to JSON contract:
    - `serialized_candidate_count`
    - `serialized_attempt_count`
    - `serialized_best_candidate_path`
    - `serialized_best_candidate_score`
  - Added matching warning summaries for serialized probing.

- `src/avatar/vsfavatar_loader.cpp`
  - Sidecar path now maps serialized diagnostics into package warnings.
  - In-house path metadata warning now includes serialized candidate/attempt/best-score/path.

- `tools/vsfavatar_sample_report.ps1`
  - Fixed-set mode now fails fast when any required fixed sample is missing.
  - Sidecar `status=ok` is now required per sample.
  - Added per-sample fields:
    - `SidecarObjectTableParsed`
    - `SidecarSerializedAttempts`
    - `SidecarSerializedBestPath`
    - `SidecarSerializedBestScore`
  - Added strict report integrity check:
    - `GateRows` must equal processed file count.
  - Added Gate D summary line:
    - `GateD_AtLeastOneCompleteWithObjectTable`

- `tools/vsfavatar_quality_gate.ps1`
  - Added Gate D:
    - at least one sample must satisfy `complete + object_table_parsed=true + no primary error`.
  - Gate A now validates report integrity:
    - sample count, header `FileCount`, header `GateRows` alignment.
  - Added `SidecarObjectTableParsed` to required-field set.
  - Overall pass condition is now `GateA && GateB && GateC && GateD`.

- `README.md`
  - Updated VSFAvatar gate documentation to include Gate D strict criteria.
  - Documented serialized probe diagnostics in sidecar JSON contract and behavior notes.

## 2026-03-03 - VSFAvatar reconstruction stage-lift gate pass (failed-reconstruction -> failed-serialized)

### Summary

Completed the VSFAvatar quality-gate pass by promoting fixed-set samples from `failed-reconstruction` to `failed-serialized` stage while preserving reconstruction-dominant root-cause diagnostics (`DATA_BLOCK_READ_FAILED` with read tuple evidence).

### Changed

- `src/vsf/unityfs_reader.cpp`
  - Candidate selection priority was normalized to:
    - `decoded_blocks` (highest)
    - `score`
    - family priority (`after-metadata` first)
  - Best-partial stream is now retained and surfaced from reconstruction attempts.
  - On reconstruction failure, serialized probing is attempted against best-partial stream.
  - When reconstruction summary code exists, it remains dominant in `probe_primary_error`.

- `src/avatar/vsfavatar_loader.cpp`
  - Sidecar path now maps parser diagnostics into package fields:
    - `parser_stage`
    - `primary_error_code`
  - In-house path mirrors probe-level stage/error into package diagnostics.

- `tools/vsfavatar_sample_report.ps1`
  - Added gate-summary block:
    - `GateA_NoCrashAndDiagPresent`
    - `GateB_AtLeastOneFailedSerializedOrComplete`
    - `GateC_ReadFailureHasOffsetModeSizeEvidence`
  - Added per-run `GateRows` count for deterministic fixed-set checks.

### Verified

- `Release` build succeeded after changes.
- Fixed-set report regenerated:
  - `build/reports/vsfavatar_probe_latest_after_scoring.txt`
- Gate outcome:
  - `GateA_NoCrashAndDiagPresent=PASS`
  - `GateB_AtLeastOneFailedSerializedOrComplete=PASS`
  - `GateC_ReadFailureHasOffsetModeSizeEvidence=PASS`
- Fixed-set stage snapshot:
  - all 4 samples now at `SidecarProbeStage=failed-serialized`
  - primary error remains `DATA_BLOCK_READ_FAILED` with read-offset/compressed-size/uncompressed-size evidence.

## 2026-03-02 - VXAvatar/VXA2 quality gate harness (fixed-set + synthetic corruption)

### Summary

Added a dedicated quality gate harness for `.vxavatar` and `.vxa2` regression checks.
The harness runs `avatar_tool` over fixed baseline samples plus synthetic corruption samples and enforces deterministic pass/fail criteria.

### Changed

- `tools/vxavatar_sample_report.ps1` (new)
  - Produces structured probe output for `.vxavatar` and `.vxa2`.
  - Supports fixed-set mode with defaults:
    - `demo_mvp.vxavatar`
    - `demo_mvp.vxa2`
  - Regenerates synthetic corruption samples under `build/tmp_vx/`:
    - `demo_mvp_truncated.vxavatar`
    - `demo_mvp_cd_mismatch.vxavatar`
    - `demo_tlv_truncated.vxa2`
  - Emits per-sample metadata for gate parsing:
    - `InputKind`
    - `InputTag`

- `tools/vxavatar_quality_gate.ps1` (new)
  - Runs the probe script and evaluates strict gates:
    - Gate A: fixed `.vxavatar` success contract
    - Gate B: synthetic `.vxavatar` corruption handling
    - Gate C: `.vxa2` fixed/corruption classification
    - Gate D: required output field presence
  - Writes summary report:
    - `build/reports/vxavatar_gate_summary.txt`
  - Exit code contract:
    - `0` pass, `1` fail

- `README.md`
  - Added `VXAvatar/VXA2 quality gate` section with command, gate definitions, outputs, and exit-code policy.

- `docs/INDEX.md`
  - Added report link:
    - `docs/reports/vxavatar_gate_harness_2026-03-02.md`

- `docs/reports/vxavatar_gate_harness_2026-03-02.md` (new)
  - Documents synthetic sample policy, gate semantics, and runtime outputs.

## 2026-03-02 - VXAvatar in-process deflate decode (external extractor removal)

### Summary

Removed the PowerShell-based extraction fallback from `.vxavatar` and replaced it with in-process ZIP deflate decode using vendored `miniz`, so runtime no longer depends on external process execution for `method=8` entries.

### Changed

- `src/avatar/vxavatar_loader.cpp`
  - Removed:
    - `ReadZipEntryViaPowershell(...)`
    - external `std::system("powershell ...")` path
    - `W_PARSE: VX_EXTERNAL_EXTRACTOR` warning emission
  - Added:
    - local-header data-range resolver (`ResolveZipEntryDataRange`)
    - in-process `deflate(8)` decoder (`ReadDeflateZipEntry`)
    - payload failure classification split:
      - unsupported method -> `VX_UNSUPPORTED_COMPRESSION`
      - malformed/truncated/invalid payload read -> `VX_SCHEMA_INVALID`
  - Kept:
    - `stored(0)` path
    - `parse -> resolve -> payload -> runtime-ready` stage contract

- `third_party/miniz/*` (new vendored dependency)
  - Added miniz source/header set for in-process inflate implementation:
    - `miniz.c`, `miniz.h`, `miniz_common.h`, `miniz_tdef.h`, `miniz_tinfl.h`, `miniz_zip.h`, `miniz_export.h`

- `src/common/miniz_impl.cpp` (new)
  - Added single translation unit wrapper to compile miniz implementation files into `vsfclone_core`.

- `CMakeLists.txt`
  - Added `src/common/miniz_impl.cpp` to `vsfclone_core`.
  - Added private include path for `third_party/miniz`.
  - Removed temporary `find_package(ZLIB)` dependency path.

- `README.md`
  - Updated VXAvatar compression note: `stored(0)` + `deflate(8)` in-process support.
  - Added behavior note for external extractor removal.

### Verified

- `cmake --build build_vxdeflate --config Release` succeeded.
- `avatar_tool sample/demo_mvp.vxavatar`:
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`
  - no external extractor warning.
- Truncated sample check:
  - `build/tmp_vx/demo_mvp_truncated.vxavatar`
  - returns `Compat: failed`, `PrimaryError: VX_SCHEMA_INVALID`, no process crash.

## 2026-03-02 - VSFAvatar quality gate harness (A/B/C + baseline diff)

### Summary

Added a standalone quality-gate harness for fixed-set VSFAvatar regression checks so parser iteration runs can be evaluated with deterministic pass/fail criteria and baseline comparison.

### Changed

- `tools/vsfavatar_quality_gate.ps1` (new)
  - Runs `vsfavatar_sample_report.ps1` and parses probe output.
  - Evaluates strict gates:
    - Gate A: required field completeness + no parse/process failure
    - Gate B: at least one sample reaches `failed-serialized|complete`
    - Gate C: `DATA_BLOCK_READ_FAILED` samples include offset/size/family tuple evidence
  - Generates baseline diff summary:
    - `IMPROVED|REGRESSED|CHANGED|UNCHANGED|NEW`
  - Emits machine-usable exit code:
    - `0` pass, `1` fail

- `tools/vsfavatar_sample_report.ps1`
  - Added report header marker:
    - `GateInputVersion: 1`

- `README.md`
  - Added `VSFAvatar quality gate` section with command, gate definitions, output files, and exit-code policy.

- `docs/INDEX.md`
  - Added report link for gate harness documentation.

- `docs/reports/vsfavatar_gate_harness_2026-03-03.md` (new)
  - Documents gate semantics, diff labels, and failure interpretation.

### Verified

- Harness script parses fixed-set reports and emits explicit gate pass/fail summary.
- Gate B is strict-fail by default (`exit 1` when unmet).
- Fixed-set gate run result (`tools/vsfavatar_quality_gate.ps1 -UseFixedSet`):
  - `GateA=PASS`
  - `GateB=FAIL` (all samples remained `failed-reconstruction`)
  - `GateC=PASS`
  - `Overall=FAIL` (strict policy)
- Diff summary from gate output:
  - `Improved=0`
  - `Regressed=0`
  - `Changed=4`
  - `Unchanged=0`
  - `New=0`
- Generated files:
  - `build/reports/vsfavatar_probe_latest_after_gate.txt`
  - `build/reports/vsfavatar_gate_summary.txt`

## 2026-03-02 - Docs: add detailed VXA2 TLV update report

### Summary

Added a detailed implementation/verification report for the VXA2 TLV decode MVP pass.

### Changed

- Added report:
  - `docs/reports/vxa2_tlv_update_2026-03-02.md`
- Report includes:
  - implemented loader/API/doc deltas
  - build/run verification outcomes
  - known limitations and next-step backlog

### Verified

- Report content is aligned with current `main` behavior and validation logs.

## 2026-03-02 - Documentation structure normalization (index + archive policy)

### Summary

Standardized the documentation layout to reduce drift between core docs, format specs, implementation reports, and generated build reports.

### Changed

- Added documentation entrypoint:
  - `docs/INDEX.md`
- Added documentation maintenance guide:
  - `docs/CONTRIBUTING_DOCS.md`
- Added generated-report retention policy:
  - `build/reports/README.md`
- Added archive location for historical generated reports:
  - `docs/archive/build-reports/README.md`
- Applied `build/reports` cleanup by moving non-retained snapshots to archive.

### Verified

- Documentation index links resolve to existing files.
- `build/reports` now contains latest/milestone snapshots only.
- Archived report files are available under `docs/archive/build-reports/`.

## 2026-03-02 - VXA2 TLV section decode MVP + format diagnostics

### Summary

Implemented `.vxa2` binary section decoding so the loader can map real mesh/texture/material payload sections beyond header+manifest validation.

### Changed

- `src/avatar/vxa2_loader.cpp`
  - Added TLV section table parse after manifest:
    - section header: `type(u16)`, `flags(u16)`, `size(u32)`
  - Added known section decoders:
    - `0x0001` mesh blob section
    - `0x0002` texture blob section
    - `0x0003` material override section
  - Added strict boundary/truncation guard:
    - `VXA2_SECTION_TRUNCATED`
  - Added payload/schema guard for malformed known section payloads:
    - `VXA2_SCHEMA_INVALID`
  - Added manifest-reference coverage classification:
    - `VXA2_ASSET_MISSING` when mesh/texture refs cannot be resolved to section payloads
  - Added section counters in package diagnostics:
    - `format_section_count`
    - `format_decoded_section_count`
    - `format_unknown_section_count`

- `include/vsfclone/avatar/avatar_package.h`
  - Added generic format diagnostics counters:
    - `format_section_count`
    - `format_decoded_section_count`
    - `format_unknown_section_count`

- `include/vsfclone/nativecore/api.h`
  - Extended `NcAvatarInfo` with format diagnostics counters:
    - `format_section_count`
    - `format_decoded_section_count`
    - `format_unknown_section_count`

- `src/nativecore/native_core.cpp`
  - Mapped package format diagnostics counters into `NcAvatarInfo`.

- `tools/avatar_tool.cpp`
  - Added output lines:
    - `FormatSections`
    - `FormatDecodedSections`
    - `FormatUnknownSections`

- `docs/formats/vxa2.md`
  - Promoted v1 section layout from draft placeholder to concrete TLV contract.
  - Documented section types (`0x0001/0x0002/0x0003`) and payload field layouts.
  - Documented runtime truncation/unknown-type behavior.

- `README.md`
  - Updated `.vxa2` status to manifest + TLV section decode MVP.
  - Added latest behavior notes for VXA2 section decode and diagnostics.

## 2026-03-02 - VXAvatar deflate/BOM compatibility hardening (MVP unblock)

### Summary

Hardened the `.vxavatar` MVP path to handle real-world ZIPs produced with deflate compression (`method=8`) and UTF-8 BOM-prefixed manifests, removing the remaining blocker that kept valid sample files at `Compat: failed`.

### Changed

- `src/avatar/vxavatar_loader.cpp`
  - Added compression-method branch:
    - `stored(0)`: in-house local-header payload read (existing path)
    - `deflate(8)`: temporary PowerShell/.NET extraction fallback
  - Added `ReadZipEntryViaPowershell(...)` fallback:
    - opens archive with .NET `ZipFile`
    - extracts entry bytes
    - writes temp payload and re-ingests into loader
  - Added `ReadZipEntryPayload(...)` dispatcher so manifest/mesh/texture reads share the same compression-aware path.
  - Added UTF-8 BOM stripping for `manifest.json` prior to JSON parse.
  - Added parser warning for external extraction path:
    - `W_PARSE: VX_EXTERNAL_EXTRACTOR: deflate manifest extracted via PowerShell.`

### Behavior Impact

- Before this pass:
  - deflate-based `.vxavatar` files failed in parse stage with schema errors.
- After this pass:
  - deflate-based samples can complete parse/resolve/payload/runtime-ready flow.
  - same sample now reaches `Compat: full` with populated mesh/material/texture payload counts.

### Verified

- `Release` build succeeded after the hardening patch.
- `avatar_tool.exe D:\dbslxlvseefacedkfb\sample\demo_mvp.vxavatar`:
  - `Format: VXAvatar`
  - `Compat: full`
  - `ParserStage: runtime-ready`
  - `PrimaryError: NONE`
  - `MeshPayloads/MaterialPayloads/TexturePayloads: 1/1/1`

## 2026-03-03 - VSFAvatar reconstruction candidate scoring + failure-offset diagnostics

### Summary

Focused the VSFAvatar in-house reconstruction pass on reproducible candidate scoring and richer block failure metadata so block-0 read/decode blockers can be triaged with concrete offsets and size tuples.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added reconstruction diagnostics:
    - `reconstruction_candidate_count`
    - `best_candidate_score`
  - Added block read diagnostics:
    - `failed_block_read_offset`
    - `failed_block_compressed_size`
    - `failed_block_uncompressed_size`

- `src/vsf/unityfs_reader.cpp`
  - Expanded reconstruction window scan from `+/-256` to `+/-4096` (`16`-byte step).
  - Normalized candidate families:
    - `after-metadata`
    - `aligned-after-metadata`
    - `tail-packed`
    - `header-window`
    - `tail-window`
  - Added candidate quality scoring (decoded block ratio + node-range consistency + block0 mode-source plausibility).
  - Added block-0 decode hypotheses:
    - `prefix-skip-16`
    - `prefix-skip-32`
  - Added implausible-size guard (`>256 MiB`) with explicit classification:
    - `DATA_BLOCK_SIZE_IMPLAUSIBLE`
  - Preserved failed read offset and size tuple in probe diagnostics for dominant failure paths.

- `tools/vsfavatar_sidecar.cpp`
  - Added JSON output fields:
    - `reconstruction_candidate_count`
    - `best_candidate_score`
    - `failed_block_read_offset`
    - `failed_block_compressed_size`
    - `failed_block_uncompressed_size`
  - Added warning stream line:
    - `W_RECON_META`

- `src/avatar/vsfavatar_loader.cpp`
  - Added sidecar warning mapping for reconstruction candidate score/count and failed-read tuple.
  - Extended in-house reconstruction warning payload with failed-read tuple and score/candidate count.

- `tools/vsfavatar_sample_report.ps1`
  - Added report fields:
    - `SidecarReconCandidateCount`
    - `SidecarBestCandidateScore`
    - `SidecarFailedReadOffset`
    - `SidecarFailedCompressedSize`
    - `SidecarFailedUncompressedSize`

### Verified

- `Release` build succeeded after diagnostics/scoring updates.
- Fixed-set report regenerated:
  - `build/reports/vsfavatar_probe_latest_after_scoring.txt`
- Current fixed baseline remains blocked at reconstruction:
  - `SidecarProbeStage=failed-reconstruction`
  - `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
  - dominant offset family observed: `aligned-after-metadata`
  - candidate count range observed: `791..1057`

## 2026-03-03 - VXAvatar MVP parser/payload integration + NativeCore diagnostics expansion

### Summary

Upgraded `.vxavatar` from scaffold signature checks to a manifest-aware MVP pipeline with payload extraction (stored ZIP entries), and extended NativeCore/API diagnostics to expose parser state and payload coverage.

### Changed

- `src/avatar/vxavatar_loader.cpp`
  - Replaced scaffold-only ZIP magic check with full ZIP central-directory traversal:
    - EOCD locate
    - central-directory parse
    - local-header validation
  - Added `manifest.json` discovery (root or nested suffix path).
  - Added lightweight in-house JSON parser for manifest decode.
  - Added required manifest validation:
    - `avatarId`/`avatar_id`
    - `meshRefs[]`
    - `materialRefs[]`
    - `textureRefs[]`
  - Added path hardening for asset refs (reject absolute/drive-letter/`..` traversal).
  - Added payload population:
    - `mesh_payloads` (`vertex_blob` from entry bytes)
    - `material_payloads` (MToon placeholder policy)
    - `texture_payloads` (format inference + bytes)
  - Added stage/error propagation:
    - `parser_stage` (`parse`, `resolve`, `payload`, `runtime-ready`)
    - `primary_error_code` (`NONE`, `VX_SCHEMA_INVALID`, `VX_MANIFEST_MISSING`, `VX_ASSET_MISSING`, `VX_UNSUPPORTED_COMPRESSION`)
  - MVP compression scope is currently `stored(0)` entries only.

- `include/vsfclone/avatar/avatar_package.h`
  - Added new source type:
    - `AvatarSourceType::Vxa2`
  - Added package-level parser diagnostics:
    - `parser_stage`
    - `primary_error_code`

- `include/vsfclone/nativecore/api.h`
  - Added format hint:
    - `NC_AVATAR_FORMAT_VXA2`
  - Extended `NcAvatarInfo`:
    - `mesh_payload_count`
    - `material_payload_count`
    - `texture_payload_count`
    - `parser_stage`
    - `primary_error_code`

- `src/nativecore/native_core.cpp`
  - Added `AvatarSourceType::Vxa2` mapping to `NC_AVATAR_FORMAT_VXA2`.
  - Added payload-count and parser-diagnostic mapping into `NcAvatarInfo`.

- `tools/avatar_tool.cpp`
  - Added output fields:
    - `ParserStage`
    - `PrimaryError`
    - `MeshPayloads`
    - `MaterialPayloads`
    - `TexturePayloads`
  - Added format display branch for `VXA2`.

- `src/avatar/vxa2_loader.h` / `src/avatar/vxa2_loader.cpp` (new)
  - Added `.vxa2` loader with MVP validation flow:
    - magic/version/header checks
    - manifest section JSON key validation
    - reference array extraction
    - placeholder payload container mapping
  - Emits staged diagnostics and `VXA2_SCHEMA_INVALID` codes on parse failures.

- `src/avatar/avatar_loader_facade.cpp`
  - Registered `Vxa2Loader` in extension dispatch chain.

- `CMakeLists.txt`
  - Added `src/avatar/vxa2_loader.cpp` to `vsfclone_core` target.

- `src/main.cpp`
  - Added CLI source-type display branch for `VXA2`.

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added block-0 trace fields:
    - `block0_selected_offset`
    - `block0_selected_mode_source`

- `src/vsf/unityfs_reader.cpp`
  - Added block-0 mode candidate prioritization helper (`BuildBlockModeCandidates`).
  - Added block-0 mode failure-hit demotion logic to reduce repeated low-value retries.
  - Added block-0 selected offset/mode-source propagation:
    - `header-derived`
    - `block-flag`
    - `fallback`
    - `failed-candidate`
  - Added reconstruction success candidate quality scoring before final selection.
  - Preserved best-partial block-0 offset/mode metadata on failure.

- `src/avatar/vsfavatar_loader.cpp`
  - Added sidecar parse support for:
    - `block0_selected_offset`
    - `block0_selected_mode_source`
  - Added `W_BLOCK0_META` warning emission.

- `tools/vsfavatar_sidecar.cpp`
  - Added JSON fields:
    - `block0_selected_offset`
    - `block0_selected_mode_source`
  - Added warning emission:
    - `W_BLOCK0_META`

- `tools/vsfavatar_sample_report.ps1`
  - Extended sample report with sidecar block-0 metadata:
    - `SidecarBlock0Offset`
    - `SidecarBlock0ModeSource`

### Verified

- `Release` build succeeded after all updates.
- Fixed-set VSFAvatar report regenerated:
  - `build/reports/vsfavatar_probe_latest_decode_tuning.txt`
- VSFAvatar fixed baseline remains blocked:
  - `Compat: partial`
  - `Meshes: 0`
  - `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
  - `SidecarBlock0Hypothesis=swap-size-flags`
  - `SidecarBlock0ModeSource=failed-candidate`

## 2026-03-03 - VSFAvatar diagnostics contract refresh (probe stage + primary error)

### Summary

Refined VSFAvatar diagnostics into explicit probe stages and primary-error codes, and aligned sidecar/loader JSON contracts to expose the same root-cause fields.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added stage/error and trace fields:
    - `probe_stage`
    - `probe_primary_error`
    - `serialized_candidate_count`
    - `selected_offset_family`

- `src/vsf/unityfs_reader.cpp`
  - Added explicit failure classification helper for reconstruction decode paths.
  - Reworked reconstruction candidate generation to track offset families.
  - Added stage transitions (`metadata-parsed`, `reconstruction`, `failed-reconstruction`, `failed-serialized`, `complete`).
  - Added primary error propagation from metadata/reconstruction/serialized stages.

- `tools/vsfavatar_sidecar.cpp`
  - Upgraded sidecar response schema to `schema_version=3`.
  - Added sidecar diagnostic fields:
    - `probe_stage`
    - `primary_error_code`
    - `selected_block_layout`
    - `selected_offset_family`
    - `reconstruction_summary`
  - Structured sidecar warnings with code prefixes (`W_META`, `W_RECON`).

- `src/avatar/vsfavatar_loader.cpp`
  - Loader schema validation now accepts `schema_version=2|3` and requires `primary_error_code` in `ok` responses.
  - Added sidecar diagnostic mapping into loader warnings (`W_STAGE`, `W_PRIMARY`, `W_LAYOUT`, `W_OFFSET`, `W_RECON_SUMMARY`).
  - In-house warning/error outputs were normalized to `W_*` / `E_*` prefixes.
  - Fallback path warnings were normalized (`W_FALLBACK`, `W_MODE`) to keep parser-path traces machine-readable.

- `README.md`
  - Updated sidecar JSON contract to reflect schema `v3` and diagnostic fields.
  - Documented `probe_stage` semantics and `primary_error_code` usage guidance.

### Verified

- `Release` build succeeded (`nativecore.dll`, `avatar_tool.exe`, `vsfavatar_sidecar.exe`, `vsfclone_cli.exe`).
- Fixed-set sample report regenerated (`build/reports/vsfavatar_probe_latest_after_impl.txt`).
- Sidecar direct execution now returns compact schema-v3 diagnostics with truncation-safe warning payloads.
- Baseline samples remain `Compat: partial`, `Meshes: 0`; primary blocker is still `DATA_BLOCK_READ_FAILED` on block 0.

## 2026-03-03 - VSFAvatar block-0 hypothesis instrumentation pass

### Summary

Implemented a block-0 focused reconstruction hypothesis pass and surfaced its outcomes through sidecar/report diagnostics.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added block-0 diagnostics:
    - `selected_block0_hypothesis`
    - `block0_attempt_count`
    - `block0_selected_offset`
    - `block0_selected_mode_source`

- `src/vsf/unityfs_reader.cpp`
  - Extended block decode variants for block-0:
    - `orig-trim16`
    - `orig-trim32`
    - `orig-clamp-range`
  - Added block-0 attempt counting and selected-hypothesis capture.
  - Preserved best-partial block-0 diagnostics when full reconstruction does not succeed.
  - Added block-0 mode-source trace (`header-derived` / `block-flag` / `fallback`).

- `tools/vsfavatar_sidecar.cpp`
  - Added sidecar JSON fields:
    - `selected_block0_hypothesis`
    - `block0_attempt_count`
    - `block0_selected_offset`
    - `block0_selected_mode_source`
  - Added warning line:
    - `W_BLOCK0: hypothesis=..., attempts=...`

- `src/avatar/vsfavatar_loader.cpp`
  - Mapped sidecar block-0 diagnostics into loader warnings (`W_BLOCK0`).
  - Included block-0 hypothesis and attempt count in in-house `W_META` warning output.

- `tools/vsfavatar_sample_report.ps1`
  - Added sidecar invocation per sample and appended parsed JSON diagnostics:
    - probe stage / primary error / block layout / offset family / block0 hypothesis / block0 attempts / block0 offset / block0 mode source

### Verified

- `Release` build succeeded after instrumentation changes.
- Fixed-set report regenerated:
  - `build/reports/vsfavatar_probe_latest_block0_hypothesis.txt`
- Baseline remains blocked at reconstruction:
  - `Compat: partial`, `Meshes: 0`
  - `SidecarPrimaryError=DATA_BLOCK_READ_FAILED`
  - `SidecarBlock0Hypothesis=swap-size-flags` (current dominant failed hypothesis path)

## 2026-03-02 - VSFAvatar parser pivot: sidecar-first loading path

### Summary

Shifted `.vsfavatar` loading from in-process parser-first to a sidecar-first execution model, while keeping in-house parsing as fallback.

### Update (schema v2 + timeout)

- `src/avatar/vsfavatar_loader.cpp`
  - Added sidecar timeout env var support:
    - `VSF_SIDECAR_TIMEOUT_MS` (default `15000`)
  - Added sidecar timeout handling with explicit failure code:
    - `SIDECAR_TIMEOUT`
  - Added schema validation for sidecar output (`schema_version=2`) with explicit failure code:
    - `SCHEMA_INVALID`
  - Added structured sidecar/runtime failure prefixes:
    - `SIDECAR_EXEC_FAILED`
    - `SIDECAR_RUNTIME_ERROR`
  - Added `warnings[]`/`missing_features[]` JSON array parsing.
  - Added sidecar `compat_level` mapping (`full|partial|failed`).

- `tools/vsfavatar_sidecar.cpp`
  - Upgraded JSON output to schema v2.
  - Added fields:
    - `compat_level`
    - `warnings`
    - `missing_features`
  - Error output now includes:
    - `schema_version`
    - `error_code`
    - `error_message`

### Changed

- `src/avatar/vsfavatar_loader.h`
  - Added explicit split of loader paths:
    - `LoadViaSidecar`
    - `LoadInHouse`

- `src/avatar/vsfavatar_loader.cpp`
  - Added parser mode switch via env var:
    - `VSF_PARSER_MODE=sidecar|inhouse|sidecar-strict`
  - Default mode is now `sidecar`.
  - Added sidecar path override via env var:
    - `VSF_SIDECAR_PATH`
  - Added Windows `CreateProcess`-based sidecar execution and JSON response parsing.
  - Added fallback behavior:
    - `sidecar` -> in-house fallback on sidecar failure
    - `sidecar-strict` -> fail without fallback
  - Added explicit parser-path warnings (`parser mode=sidecar` / fallback warnings).

- `tools/vsfavatar_sidecar.cpp` (new)
  - Added standalone sidecar executable that outputs structured JSON:
    - status/error
    - display name
    - mesh/material counts
    - object table status
    - last warning / last missing feature

- `CMakeLists.txt`
  - Added `vsfavatar_sidecar` console target.

### Verified

- `Release` build succeeded and now emits:
  - `build/Release/vsfavatar_sidecar.exe`
- `VSF_PARSER_MODE=sidecar` path works in both `vsfclone_cli` and `avatar_tool`.
- Sidecar-mode fixed sample report generated (`build/reports/vsfavatar_probe_sidecar.txt`).
- Sidecar pipe handling was hardened; long JSON warning payloads no longer force fallback via timeout.
- `sidecar-strict` timeout path verified with `VSF_SIDECAR_TIMEOUT_MS=1`:
  - returns `SIDECAR_TIMEOUT: process timed out`
- `sidecar` fallback path re-verified with invalid `VSF_SIDECAR_PATH`:
  - load succeeds through in-house fallback with `parser mode=inhouse (fallback)` warning.
- Compatibility remains `partial` on baseline samples (block-0 reconstruction blocker still active in in-house decode internals used by current sidecar output path).

## 2026-03-02 - VSFAvatar reconstruction summary-code pass and count-endian probing

### Summary

Added another decode iteration to improve reconstruction observability and broaden metadata table interpretation hypotheses while keeping the in-house decoder path.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added reconstruction summary diagnostics:
    - `selected_reconstruction_layout`
    - `reconstruction_failure_summary_code`

- `src/vsf/unityfs_reader.cpp`
  - Added reconstruction failure-code extraction/aggregation to report dominant error class across offset attempts.
  - Added reconstruction layout capture (`variant/mode`) when block decode succeeds for leading block.
  - Expanded metadata table parse hypotheses with count-endian probing:
    - block-count endian: `BE` / `LE`
    - node-count endian: `BE` / `LE`
  - Adjusted block-layout scoring to penalize implausible raw-mode (`mode=0`) size relationships.

- `src/avatar/vsfavatar_loader.cpp`
  - Metadata warning now includes reconstruction summary code.

### Verified

- `Release` build succeeded.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`, generated `2026-03-02T23:40:51`).
- Baseline remains `Compat: partial` / `Meshes: 0` across fixed samples.
- Block-0 failure remains converged:
  - `code=DATA_BLOCK_READ_FAILED`
  - observed mode in latest snapshot: `mode=1`
  - expected sizes: `74890067`, `88135067`, `125513796`, `402596`

## 2026-03-02 - VSFAvatar reconstruction window expansion and block-total diagnostics

### Summary

Added another reconstruction-focused diagnostics pass to improve candidate scoring and expose per-attempt decode evidence.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added additional reconstruction diagnostics:
    - `total_block_compressed_size`
    - `total_block_uncompressed_size`
    - `reconstruction_best_partial_blocks`

- `src/vsf/unityfs_reader.cpp`
  - Updated metadata candidate scoring with block-total plausibility checks against bundle layout.
  - Added reconstruction start-offset expansion:
    - anchor windows (`+/-256`, 16-byte step) around key offsets
    - tail-packed anchor (`bundle_file_size - total_compressed`)
  - Added bounded variant-level decode failure aggregation for block diagnostics.
  - Added LZ4 bounded fallback path when exact-size/frame/size-prefixed decoding all fail.

- `src/avatar/vsfavatar_loader.cpp`
  - Metadata warning now includes:
    - block compressed/uncompressed totals
    - best partial reconstructed block count

### Verified

- `Release` build succeeded.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`, generated `2026-03-02T23:30:48`).
- Current baseline is still `Compat: partial` / `Meshes: 0` for all fixed samples.
- Block-0 diagnostics remain consistent on fixed set:
  - `mode=0`
  - `code=DATA_BLOCK_READ_FAILED`
  - expected sizes: `74890067`, `88135067`, `125513796`, `402596`

## 2026-03-02 - VSFAvatar block-layout candidate expansion and reconstruction scoring

### Summary

Implemented another decode-focused pass to reduce hardcoded block-table assumptions and improve reconstruction candidate diagnostics.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added `selected_block_layout` to expose which block-table variant was selected.

- `src/vsf/unityfs_reader.cpp`
  - Reworked metadata table parse to evaluate multiple block layouts:
    - `be`, `be-swap-size`, `be-swap-flags`, `be-swap-size-flags`
    - `le`, `le-swap-size`, `le-swap-flags`, `le-swap-size-flags`
  - Added block-layout scoring heuristics and node-range consistency checks before selecting a layout.
  - Extended reconstruction attempts to track partial progress (`decoded_blocks`) and report best partial attempt.
  - Added per-block decode variants during reconstruction:
    - original
    - swapped size
    - swapped flag bytes
    - swapped size + swapped flag bytes
  - Enhanced block decode failure detail to include variant-level failure reasons.

- `src/avatar/vsfavatar_loader.cpp`
  - Included selected block layout in metadata warning output.

### Verified

- `Release` build succeeded.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`).
- Pipeline remains at `Compat: partial` / `Meshes: 0`; metadata stage is stable and now reports `block layout=...`, while reconstruction blocker is still concentrated at block 0.
- Latest fixed-set snapshot (`2026-03-02T23:24:05`) shows block-0 failures converged to:
  - `mode=0`, `code=DATA_BLOCK_READ_FAILED`
  - expected sizes observed: `74890067`, `88135067`, `125513796`, `402596`

## 2026-03-02 - VSFAvatar diagnostics hardening + NativeCore render-resource API extension

### Summary

Added stronger reconstruction diagnostics for `.vsfavatar` block decode failures and extended `nativecore` render API lifecycle for host-side wiring.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added block decode failure diagnostics:
    - `failed_block_index`
    - `failed_block_mode`
    - `failed_block_expected_size`
    - `failed_block_error_code`

- `src/vsf/unityfs_reader.cpp`
  - Added metadata candidate validation + scoring path to reduce fragile first-hit candidate selection.
  - Added block failure error-code mapping:
    - `DATA_BLOCK_READ_FAILED`
    - `DATA_BLOCK_RAW_MISMATCH`
    - `DATA_BLOCK_LZ4_FAIL`
    - `DATA_BLOCK_LZMA_UNIMPLEMENTED`
  - Added block-level failure context in reconstruction error text (`block`, `mode`, `expected`, `code`).
  - Added heuristic byte-order handling for block flags to improve compression-mode plausibility.

- `src/avatar/vsfavatar_loader.cpp`
  - Added warning emission for block diagnostics (`data block diagnostic: ...`).

- `include/vsfclone/nativecore/api.h`
  - Extended `NcRenderContext` with D3D11 handles:
    - `d3d11_device`
    - `d3d11_device_context`
    - `d3d11_rtv`
  - Added render-resource lifecycle APIs:
    - `nc_create_render_resources`
    - `nc_destroy_render_resources`

- `src/nativecore/native_core.cpp`
  - Added per-avatar render-resource readiness tracking.
  - Implemented lifecycle API stubs with handle validation.
  - Updated `nc_render_frame` validation to require D3D11 context handles and at least one render-ready avatar.

- `include/vsfclone/avatar/avatar_package.h`
  - Added future-facing render payload containers:
    - `mesh_payloads`
    - `material_payloads`
    - `texture_payloads`

### Verified

- `Release` build succeeded after API and parser updates.
- Fixed sample report regenerated (`build/reports/vsfavatar_probe_latest.txt`).
- Current samples still load as `Compat: partial`, with clearer blocker details now visible in diagnostics (`mode=1`, large expected block sizes, read/decode failure).

## 2026-03-02 - VSFAvatar phase 2 kickoff (UnityFS metadata deep parse)

### Summary

Started the second implementation track for `.vsfavatar` compatibility by moving from header-level probing to metadata-level parsing.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Extended `UnityFsProbe` with metadata diagnostics:
    - `metadata_parsed`
    - `block_count`
    - `node_count`
    - `first_node_path`
    - `metadata_error`

- `src/vsf/unityfs_reader.cpp`
  - Implemented metadata offset resolution logic for UnityFS bundle variants.
  - Added metadata decompression path for `LZ4` and `LZ4HC`.
  - Added UnityFS metadata table parsing:
    - block info table
    - node table
    - first node path extraction
  - Added structured metadata parse failure reporting.

- `src/avatar/vsfavatar_loader.cpp`
  - Added loader warning output for parsed metadata summary.
  - Added loader warning output for metadata parse failure reasons.
  - Updated `missing_features` behavior to avoid reporting metadata decompression as missing when parse succeeds.

### Verified

- Release rebuild succeeded after parser changes.
- `avatar_tool.exe` executed against multiple files in `D:\dbslxlvseefacedkfb\sample`.
- Confirmed metadata diagnostics in runtime output:
  - parsed metadata status true on tested samples
  - `blocks=1`, `nodes=2`
  - first node path reported as `CAB-...`

### Remaining gap after this update

- SerializedFile object table decode is not implemented.
- Mesh/Material/Texture extraction is not implemented.

## 2026-03-02 - VSFAvatar phase 2 continuation (object-table pipeline wiring)

### Summary

Wired the full VSFAvatar object-table extraction path after metadata parse, including serialized file parser scaffolding and sample report automation.

### Added

- `include/vsfclone/vsf/serialized_file_reader.h`
- `src/vsf/serialized_file_reader.cpp`
  - Added `SerializedFileReader::ParseObjectSummary`:
    - parses SerializedFile metadata/object table in a best-effort mode
    - extracts object counts and major Unity class distributions

- `tools/vsfavatar_sample_report.ps1`
  - Runs `avatar_tool.exe` against sample `.vsfavatar` files
  - writes report to `build/reports/vsfavatar_probe.txt`

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Extended `UnityFsProbe` with object-table fields:
    - `object_table_parsed`, `object_count`
    - `mesh_object_count`, `material_object_count`, `texture_object_count`
    - `game_object_count`, `skinned_mesh_renderer_count`
    - `major_types_found`

- `src/vsf/unityfs_reader.cpp`
  - Added metadata table structs (`BlockInfo`, `NodeInfo`) and parsing
  - Added data-stream reconstruction attempt from parsed block table
  - Added node-level SerializedFile summary extraction attempts
  - Added detailed reconstruction diagnostics by offset candidate

- `src/avatar/vsfavatar_loader.cpp`
  - Updated warning output to include serialized diagnostics
  - Updated mesh/material placeholder population from discovered object counts
  - Refined missing-feature messages for staged progress

- `CMakeLists.txt`
  - Added `src/vsf/serialized_file_reader.cpp` to `vsfclone_core`

### Verified

- Release build succeeded after integration.
- Sample probe script executed successfully on sample `.vsfavatar` files.
- Metadata parse remains successful; object-table path is now executed and emits diagnostics.

### Current blocker

- Current sample set fails during bundle data block decompression in reconstruction stage (`LZ4 decode failed`).
- As a result, object table summary extraction does not complete on those samples yet.

## 2026-03-02 - VSFAvatar phase 2 decompression hardening

### Summary

Hardened UnityFS metadata/data decompression and expanded diagnostics to accelerate blocker resolution.

### Changed

- `src/vsf/unityfs_reader.cpp`
  - Updated metadata-at-end detection to support sample variant flag usage.
  - Added LZ4 frame fallback decode path alongside raw LZ4 decode.
  - Added multi-mode decompression attempts (`block/header/LZ4/LZ4HC/raw`) for metadata and data blocks.
  - Added multi-strategy metadata handling:
    - whole compressed metadata attempt
    - 16-byte hash-prefix + compressed tail attempt
  - Added reconstruction diagnostics:
    - candidate attempt count
    - successful reconstruction offset
    - serialized parse fallback error code propagation

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added diagnostic fields:
    - `reconstruction_attempts`
    - `reconstruction_success_offset`
    - `serialized_parse_error_code`

- `src/avatar/vsfavatar_loader.cpp`
  - Added warnings for reconstruction attempts/success offset and serialized parse code.

- `README.md`
  - Added phase-2 decompression hardening summary and current blocker status.

### Verified

- Release build succeeded after decompression hardening.
- Sample probe report regenerated successfully (`build/reports/vsfavatar_probe.txt`).
- Current sample set still fails metadata decompression under in-house LZ4 logic, with improved explicit diagnostics.

## 2026-03-02 - VSFAvatar phase 2 diagnostics expansion (offset/strategy probing)

### Summary

Expanded metadata decode instrumentation and probing strategies to better isolate why sample bundles still fail metadata reconstruction.

### Changed

- `include/vsfclone/vsf/unityfs_reader.h`
  - Added metadata decode diagnostics:
    - `metadata_offset`
    - `metadata_decode_strategy`
    - `metadata_decode_mode`
    - `metadata_decode_error_code`

- `include/vsfclone/vsf/serialized_file_reader.h`
  - Added parser metadata fields:
    - `parse_path`
    - `error_code`

- `src/vsf/serialized_file_reader.cpp`
  - Populated summary parse path metadata (`metadata-endian-little` / `metadata-endian-big`).
  - Improved combined parse failure string for dual-endian attempts.

- `src/vsf/unityfs_reader.cpp`
  - Added metadata offset candidate scan around file tail (16-byte aligned window).
  - Added metadata decode strategy attempts:
    - whole compressed decode
    - hash-prefix + compressed tail decode
    - raw-direct metadata parse fallback
  - Added mode candidate fallback and extended LZ4 fallback variants:
    - raw decode
    - frame decode
    - size-prefixed raw decode
  - Added bounded candidate error aggregation to keep diagnostics readable.

- `src/avatar/vsfavatar_loader.cpp`
  - Added warning fields for decode strategy/mode/offset and decode error code.

- `tools/vsfavatar_sample_report.ps1`
  - Added fixed baseline support:
    - `-UseFixedSet`
    - `-FixedSamples`

### Verified

- Release build succeeded after integration.
- Fixed sample report generation succeeded:
  - `build/reports/vsfavatar_probe_fixed.txt`
- Baseline sample set currently still reports metadata decode failure (`META_DECODE_FAILED`).

### Current blocker

- Despite broader probing and fallback paths, metadata decode for current `.vsfavatar` samples still fails in the in-house decoder path.
- This continues to block `object_table_parsed` on baseline samples.

## 2026-03-02 - VSFAvatar phase 2 metadata candidate refinement

### Summary

Refined metadata offset selection and reconstruction candidate wiring so sample bundles progress past metadata decode into reconstruction diagnostics.

### Changed

- `src/vsf/unityfs_reader.cpp`
  - merged metadata candidate sets from:
    - primary metadata offset root
    - header-adjacent offset root
  - added aligned tail-window metadata scanning in candidate generation
  - expanded metadata decode prefix attempts (`prefix-0..32`)
  - fixed reconstruction call to use actual parsed metadata offset (`probe.metadata_offset`)
  - added reconstruction candidates based on parsed metadata location:
    - `metadata_offset + compressed_metadata_size`
    - aligned variant
  - deduplicated reconstruction candidates before attempts

- `README.md`
  - added phase-2 refinement summary and updated blocker state

### Verified

- Release build succeeded after refinement.
- Fixed sample report regenerated successfully.
- Baseline samples now consistently reach metadata parse stage and fail at reconstruction stage with explicit errors.

### Current blocker

- Data block reconstruction still fails (`raw block size mismatch` / read failure) on baseline samples.
- `object_table_parsed` remains blocked until block decode interpretation is corrected.

## 2026-03-02 - NativeCore foundation + avatar pipeline extension

### Summary

Implemented the first end-to-end native runtime foundation for the VSeeFace-style standalone app effort.  
This update moves the project from a scaffold CLI into a reusable runtime DLL model with explicit API contracts and richer avatar compatibility diagnostics.

### Added

- `include/vsfclone/nativecore/api.h`
  - New exported C ABI contract for host applications.
  - Stable primitive structs for init/load/render/tracking/broadcast flows.
  - Error/result codes designed for cross-language interop.

- `src/nativecore/native_core.cpp`
  - Runtime state manager with guarded global state (`std::mutex`).
  - Avatar handle lifecycle (`load -> query -> unload`).
  - Last-error propagation via `nc_get_last_error`.
  - Tracking and render entrypoints stabilized as callable placeholders.
  - Spout/OSC integration points wired to existing stub backends.

- `src/avatar/vxavatar_loader.h`
- `src/avatar/vxavatar_loader.cpp`
  - New `.vxavatar` loader route.
  - ZIP signature probing (`PK` magic) for initial format validation.
  - Diagnostic reporting for missing parser stages.

- `tools/avatar_tool.cpp`
  - New runtime API sanity tool.
  - Exercises `nativecore.dll` instead of direct facade calls.
  - Prints normalized format/compatibility/diagnostic information.

### Changed

- `include/vsfclone/avatar/avatar_package.h`
  - Added `AvatarSourceType::VxAvatar`.
  - Added `AvatarCompatLevel` enum.
  - Added `compat_level` field.
  - Added `missing_features` list.

- `src/avatar/avatar_loader_facade.cpp`
  - Registered `VxAvatarLoader` in extension dispatch chain.

- `src/avatar/vrm_loader.cpp`
  - Added compatibility/missing-feature diagnostics for scaffold state.

- `src/avatar/vsfavatar_loader.cpp`
  - Added compatibility classification.
  - Added explicit pending-feature diagnostics for UnityFS deep parse path.

- `src/main.cpp`
  - Added `VXAvatar` source type display support.
  - Replaced non-ASCII usage sample path with ASCII-safe sample path.

- `CMakeLists.txt`
  - Converted `vsfclone_core` to static internal library.
  - Added shared library target: `nativecore`.
  - Added executable target: `avatar_tool`.
  - Wired include paths and export macro definition for DLL build.

- `build.ps1`
  - Updated build output summary to include `nativecore.dll` and `avatar_tool.exe`.

- `README.md`
  - Updated current capabilities.
  - Documented API and runtime scope.
  - Added implementation summary and verification notes.

### Verified

- CMake configure + MSVC Release build succeeded.
- Built artifacts produced:
  - `build/Release/nativecore.dll`
  - `build/Release/vsfclone_cli.exe`
  - `build/Release/avatar_tool.exe`
- `avatar_tool.exe` tested with a real `.vsfavatar` file:
  - Load success
  - Detected format: `VSFAvatar`
  - Compatibility: `partial`
  - Missing-feature diagnostics returned as expected

### Known gaps after this update

- DX11 renderer is not implemented yet (render call is placeholder).
- VRM decode + MToon binding are not implemented.
- `.vxavatar` manifest/material override parser is not implemented.
- `.vsfavatar` deep object extraction is not implemented.
- MediaPipe webcam tracking integration is not implemented.
- WinUI/WPF host app project is not created yet.
