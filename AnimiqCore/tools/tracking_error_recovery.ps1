param(
    [string]$ErrorCode,
    [switch]$Execute,
    [string]$OutputTxt = ".\build\reports\tracking_error_recovery_summary.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

if ([string]::IsNullOrWhiteSpace($ErrorCode)) {
    throw "ErrorCode is required."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTxt) | Out-Null

$normalized = $ErrorCode.Trim().ToUpperInvariant()
$actionName = "OPEN_DIAGNOSTICS"
$actionCommand = ""
$actionHint = ""

switch ($normalized) {
    "TRACKING_MEDIAPIPE_CONFIG_INVALID" {
        $actionName = "VERIFY_SIDECAR_SCRIPT_PATH"
        $actionHint = "Check ANIMIQ_MEDIAPIPE_SIDECAR_SCRIPT and mediapipe_webcam_sidecar.py path."
    }
    "TRACKING_MEDIAPIPE_START_FAILED" {
        $actionName = "REBUILD_TRACKING_VENV"
        $actionCommand = "powershell -ExecutionPolicy Bypass -File .\tools\setup_tracking_python_venv.ps1"
        $actionHint = "Rebuild tracking venv and retry."
    }
    "TRACKING_MEDIAPIPE_NO_FRAME" {
        $actionName = "RUN_MEDIAPIPE_SANITY"
        $actionCommand = "powershell -ExecutionPolicy Bypass -File .\tools\mediapipe_sidecar_sanity.ps1"
        $actionHint = "Run sidecar sanity and check camera occupancy."
    }
    "TRACKING_WEBCAM_RUNTIME_UNAVAILABLE" {
        $actionName = "RUN_MEDIAPIPE_SANITY"
        $actionCommand = "powershell -ExecutionPolicy Bypass -File .\tools\mediapipe_sidecar_sanity.ps1"
        $actionHint = "Validate python/mediapipe runtime readiness."
    }
    "TRACKING_IFACIAL_NO_PACKET" {
        $actionName = "CHECK_INPUT_ENDPOINT"
        $actionHint = "Verify sender IP/port and firewall."
    }
    default {
        $actionName = "OPEN_DIAGNOSTICS"
        $actionHint = "No mapped auto-action. Open diagnostics and inspect last_error_code."
    }
}

$execStatus = "SKIPPED"
$execExit = 0
if ($Execute -and -not [string]::IsNullOrWhiteSpace($actionCommand)) {
    Push-Location $repoRoot
    try {
        Invoke-Expression $actionCommand
        $execExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $execStatus = if ($execExit -eq 0) { "PASS" } else { "FAIL" }
}

$lines = @(
    "Tracking Error Recovery Summary"
    "GeneratedUtc: $((Get-Date).ToUniversalTime().ToString('o'))"
    "ErrorCode: $normalized"
    "ActionName: $actionName"
    "ActionHint: $actionHint"
    "ActionCommand: $(if ([string]::IsNullOrWhiteSpace($actionCommand)) { '<none>' } else { $actionCommand })"
    "Execute: $Execute"
    "ExecuteStatus: $execStatus"
    "ExecuteExitCode: $execExit"
)
$lines | Set-Content -Path $resolvedTxt -Encoding UTF8
Write-Host "summary=$resolvedTxt"

if ($Execute -and $execExit -ne 0) {
    exit $execExit
}

