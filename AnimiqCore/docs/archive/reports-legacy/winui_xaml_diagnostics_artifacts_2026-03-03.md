# WinUI XAML Diagnostics Artifacts Guide (2026-03-03)

## Scope

This note describes the diagnostic artifacts collected when `host/WinUiHost` publish fails in `tools/publish_hosts.ps1`.

## Trigger

Diagnostics are collected when all of the following are true:

- `-IncludeWinUi` is enabled.
- WinUI publish fails.
- `-CollectWinUiDiagnostics` is `true` (default).

## Artifact Location

- `build/reports/winui`

Primary files:

- `winui_build.binlog`
- `winui_build_diag.log`
- `winui_build_stderr.log`
- `winui_diagnostic_manifest.json`
- `obj-dump/**` (copied from `host/WinUiHost/obj`)

## Reading Order

1. Open `winui_build.binlog` in MSBuild Structured Log Viewer.
2. Check `winui_build_stderr.log` for immediate compiler/tool errors.
3. Check `obj-dump/**/output.json` for XAML compiler context.
4. Use `winui_diagnostic_manifest.json` to confirm command line and file paths.

## CI

The `host-publish` workflow uploads `build/reports/winui` as part of the `host-publish-outputs` artifact with `if: always()`, so diagnostics remain available even on failed runs.
