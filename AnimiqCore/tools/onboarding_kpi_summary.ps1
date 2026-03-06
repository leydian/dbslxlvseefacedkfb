param(
    [string]$TelemetryPath = ".\build\reports\telemetry_latest.json",
    [string]$OutputJson = ".\build\reports\onboarding_kpi_summary.json",
    [string]$OutputTxt = ".\build\reports\onboarding_kpi_summary.txt"
)

$ErrorActionPreference = "Stop"

function Read-Bool {
    param([object]$Value)
    if ($null -eq $Value) { return $false }
    if ($Value -is [bool]) { return $Value }
    $text = "$Value".Trim()
    $parsed = $false
    [bool]::TryParse($text, [ref]$parsed) | Out-Null
    return $parsed
}

function Build-SessionStats {
    param([array]$Events)

    $sessions = @{}
    $outputStartedMilestones = 0
    $outputSuccessMilestones = 0

    foreach ($event in $Events) {
        if ($event.name -ne "onboarding_milestone") { continue }
        $sessionStartedAt = [string]$event.session_started_at
        if ([string]::IsNullOrWhiteSpace($sessionStartedAt)) { continue }

        if (-not $sessions.ContainsKey($sessionStartedAt)) {
            $sessions[$sessionStartedAt] = [ordered]@{
                session_started_at = $sessionStartedAt
                output_started = $false
                within_3min_success = $false
            }
        }

        $milestone = [string]$event.milestone
        if ($milestone.StartsWith("output_started:", [System.StringComparison]::OrdinalIgnoreCase)) {
            $outputStartedMilestones++
            $sessions[$sessionStartedAt].output_started = $true
            $success = Read-Bool -Value $event.within_3min_success
            if ($success) {
                $outputSuccessMilestones++
                $sessions[$sessionStartedAt].within_3min_success = $true
            }
        }
    }

    $sessionList = @($sessions.Values)
    $sessionCount = $sessionList.Count
    $outputStartedSessions = @($sessionList | Where-Object { $_.output_started }).Count
    $successSessions = @($sessionList | Where-Object { $_.within_3min_success }).Count
    $successRatePct = if ($sessionCount -gt 0) { [Math]::Round((100.0 * $successSessions / $sessionCount), 2) } else { 0.0 }

    return [ordered]@{
        generated_utc = (Get-Date).ToUniversalTime().ToString("o")
        telemetry_path = (Resolve-Path -LiteralPath $TelemetryPath).Path
        onboarding_event_count = @($Events | Where-Object { $_.name -eq "onboarding_milestone" }).Count
        session_count = $sessionCount
        output_started_sessions = $outputStartedSessions
        within_3min_success_sessions = $successSessions
        within_3min_success_rate_pct = $successRatePct
        output_started_milestones = $outputStartedMilestones
        output_success_milestones = $outputSuccessMilestones
    }
}

if (-not (Test-Path -LiteralPath $TelemetryPath)) {
    throw "Telemetry file not found: $TelemetryPath"
}

$raw = Get-Content -LiteralPath $TelemetryPath -Raw -Encoding UTF8
$events = ConvertFrom-Json -InputObject $raw
if ($events -isnot [System.Collections.IEnumerable]) {
    throw "Telemetry payload must be a JSON array."
}

$stats = Build-SessionStats -Events @($events)

$outputJsonDir = Split-Path -Parent $OutputJson
if (-not [string]::IsNullOrWhiteSpace($outputJsonDir)) {
    New-Item -Path $outputJsonDir -ItemType Directory -Force | Out-Null
}
$outputTxtDir = Split-Path -Parent $OutputTxt
if (-not [string]::IsNullOrWhiteSpace($outputTxtDir)) {
    New-Item -Path $outputTxtDir -ItemType Directory -Force | Out-Null
}

$stats | ConvertTo-Json -Depth 4 | Set-Content -Path $OutputJson -Encoding UTF8

$txt = @(
    "Onboarding KPI Summary"
    "GeneratedUtc: $($stats.generated_utc)"
    "TelemetryPath: $($stats.telemetry_path)"
    "OnboardingEventCount: $($stats.onboarding_event_count)"
    "SessionCount: $($stats.session_count)"
    "OutputStartedSessions: $($stats.output_started_sessions)"
    "Within3MinSuccessSessions: $($stats.within_3min_success_sessions)"
    "Within3MinSuccessRatePct: $($stats.within_3min_success_rate_pct)"
    "OutputStartedMilestones: $($stats.output_started_milestones)"
    "OutputSuccessMilestones: $($stats.output_success_milestones)"
)
$txt -join [Environment]::NewLine | Set-Content -Path $OutputTxt -Encoding UTF8

Write-Host "onboarding KPI summary generated"
Write-Host "json: $OutputJson"
Write-Host "txt:  $OutputTxt"
