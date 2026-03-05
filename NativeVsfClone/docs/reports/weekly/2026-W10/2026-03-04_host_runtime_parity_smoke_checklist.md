# Host Runtime Parity Smoke Checklist (2026-03-04)

## Purpose

Define the minimum manual parity smoke checks to run after both WPF and WinUI publish succeed.

## Preconditions

- `tools/publish_hosts.ps1 -IncludeWinUi` completed successfully.
- `dist/wpf/WpfHost.exe` and `dist/winui/WinUiHost.exe` exist.
- `build/reports/host_publish_latest.txt` confirms WPF/WinUI publish success.

## Scenarios

| ID | Scenario | Expected Result (WPF/WinUI parity) |
| --- | --- | --- |
| S1 | Launch host and load valid avatar | Session/avatar state becomes loaded; no fatal errors in logs |
| S2 | Start/stop OSC output | Output state toggles correctly; no invalid transition errors |
| S3 | Toggle broadcast/camera mode | Render state updates and controls reflect active mode |
| S4 | Change render sliders while idle | Values apply and status/logs remain stable |
| S5 | Save/apply/delete/reset render preset | Preset operations persist and round-trip correctly |
| S6 | Trigger long operation then interact with render/preset controls | Busy guard blocks conflicting interaction during operation |
| S7 | Enter invalid OSC bind/publish inputs | Inline validation appears; guarded actions remain disabled |
| S8 | Resize window during active render | Render continues without crash; diagnostics stay sane |

## Execution Notes

- Run the same scenario order on WPF and WinUI.
- Record PASS/FAIL and short evidence note per scenario.
- Any mismatch across WPF/WinUI is treated as parity regression.

## Result Template

| ID | WPF | WinUI | Notes |
| --- | --- | --- | --- |
| S1 |  |  |  |
| S2 |  |  |  |
| S3 |  |  |  |
| S4 |  |  |  |
| S5 |  |  |  |
| S6 |  |  |  |
| S7 |  |  |  |
| S8 |  |  |  |
