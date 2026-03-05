# WPF Single-File Startup Crash Recovery + Publish Guard Update (2026-03-06)

## Summary

After redeploy, `dist\wpf\WpfHost.exe` failed to open on target environment and exited immediately.

Crash signature:

- process exits in ~1-3 seconds
- Application Error: `0xe0434352` then `0xc000041d`
- `.NET Runtime` event `1026`
- unhandled exception: `System.DllNotFoundException` during WPF window subclass hook path

Root cause was the WPF publish mode default:

- `PublishSingleFile=true` in `tools/publish_hosts.ps1`
- runtime environment hit a native dependency resolution failure on startup for the single-file host shape

This pass restores stable runtime behavior by defaulting WPF publish to non-single-file output and hardening smoke validation to fail when crash events are detected.

## Impacted Surface

- `tools/publish_hosts.ps1`
- `tools/wpf_launch_smoke.ps1`
- `dist\wpf\WpfHost.exe` redeploy output

## Implementation Details

### 1) WPF publish default policy update

File:

- `tools/publish_hosts.ps1`

Changes:

- Added parameter:
  - `WpfPublishSingleFile` (bool, default: `false`)
- WPF publish arguments now use:
  - `/p:PublishSingleFile=$WpfPublishSingleFile`
- Added run log line:
  - `WpfPublishSingleFile: <true|false>`

Result:

- default deploy now emits the full runtime file set required for stable startup on this environment.

### 2) Smoke script false-positive prevention

File:

- `tools/wpf_launch_smoke.ps1`

Changes:

- Added crash event detection from Application log scan:
  - if event id in `{1026, 1000}` is observed for `WpfHost.exe`, treat run as crash
- On crash detection:
  - force `Status=FAIL`
  - force non-zero `ExitCode` (`-2` fallback when process exit code remained `0`)
- Added explicit report field:
  - `CrashDetected: true|false`

Result:

- smoke no longer reports PASS when the process briefly launches but crashes during or just after probe window.

## Verification

### Before fix

- `Start-Process dist\wpf\WpfHost.exe`:
  - `ALIVE=False`
  - `EXITCODE=-1073740771` (`0xC000041D`)
- Event log showed:
  - `.NET Runtime 1026` with `System.DllNotFoundException`
  - `Application Error 1000` for `WpfHost.exe`

### After fix + redeploy

Executed:

- `powershell -ExecutionPolicy Bypass -File .\tools\publish_hosts.ps1`

Observed:

- `build/reports/wpf_launch_smoke_latest.txt`
  - `Status: PASS`
  - `CrashDetected: false`
- direct process probe:
  - `ALIVE=True` after 5 seconds

## Operational Notes

- WPF distribution remains self-contained; only single-file bundling default changed.
- If future packaging requires single-file again, use:
  - `-WpfPublishSingleFile:$true`
  and validate startup with updated smoke script before release promotion.
