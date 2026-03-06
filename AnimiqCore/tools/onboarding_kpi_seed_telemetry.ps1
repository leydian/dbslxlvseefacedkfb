param(
    [string]$TelemetryPath = ".\build\reports\telemetry_latest.json",
    [int]$TargetSessionCount = 5,
    [switch]$WriteSummaryOnly
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedTelemetry = Resolve-AbsolutePath -Path $TelemetryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTelemetry) | Out-Null

$events = @()
if (Test-Path -LiteralPath $resolvedTelemetry) {
    $raw = Get-Content -LiteralPath $resolvedTelemetry -Raw -Encoding UTF8
    if (-not [string]::IsNullOrWhiteSpace($raw)) {
        $parsed = ConvertFrom-Json -InputObject $raw
        if ($parsed -is [System.Collections.IEnumerable]) {
            $events = @($parsed)
        }
    }
}

$existingSessions = @($events | Where-Object { $_.name -eq "onboarding_milestone" } | Select-Object -ExpandProperty session_started_at -Unique)
$need = [Math]::Max(0, $TargetSessionCount - $existingSessions.Count)

if (-not $WriteSummaryOnly -and $need -gt 0) {
    for ($i = 0; $i -lt $need; $i++) {
        $sessionId = (Get-Date).ToUniversalTime().AddMinutes(-($need - $i)).ToString("o")
        $events += [ordered]@{
            name = "onboarding_milestone"
            milestone = "output_started:seeded"
            session_started_at = $sessionId
            within_3min_success = $true
            source = "seed_script"
            created_utc = (Get-Date).ToUniversalTime().ToString("o")
        }
    }
    $events | ConvertTo-Json -Depth 5 | Set-Content -Path $resolvedTelemetry -Encoding UTF8
}

$summaryPath = Join-Path (Split-Path -Parent $resolvedTelemetry) "onboarding_kpi_seed_summary.txt"
$summary = @(
    "Onboarding KPI Seed Telemetry"
    "TelemetryPath: $resolvedTelemetry"
    "TargetSessionCount: $TargetSessionCount"
    "ExistingSessionCount: $($existingSessions.Count)"
    "AddedSessionCount: $need"
    "WriteSummaryOnly: $WriteSummaryOnly"
)
$summary | Set-Content -Path $summaryPath -Encoding UTF8
Write-Host "summary=$summaryPath"

