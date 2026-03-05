param(
    [string]$ReportDir = ".\build\reports",
    [string]$OutputJson = ".\build\reports\release_gate_dashboard.json",
    [string]$OutputTxt = ".\build\reports\release_gate_dashboard.txt"
)

$ErrorActionPreference = "Stop"

function Get-StatusFromFile {
    param([string]$Path, [string]$Pattern)
    if (-not (Test-Path $Path)) { return "MISSING" }
    $line = Select-String -Path $Path -Pattern $Pattern -SimpleMatch | Select-Object -First 1
    if ($null -eq $line) { return "UNKNOWN" }
    return $line.Line.Trim()
}

function Get-KeyValueFromFile {
    param([string]$Path, [string]$Prefix)
    if (-not (Test-Path $Path)) { return "" }
    $line = Get-Content -Path $Path | Where-Object { $_.StartsWith($Prefix) } | Select-Object -First 1
    if ($null -eq $line) { return "" }
    return $line.Substring($Prefix.Length).Trim()
}

function Get-PassFailFromStatusLine {
    param([string]$Line)
    if ([string]::IsNullOrWhiteSpace($Line)) { return "UNKNOWN" }
    if ($Line -match "\bPASS\b") { return "PASS" }
    if ($Line -match "\bFAIL\b") { return "FAIL" }
    if ($Line -eq "MISSING") { return "MISSING" }
    return "UNKNOWN"
}

function Get-HostTrackState {
    param([string]$HostReportPath)
    if (-not (Test-Path $HostReportPath)) {
        return [ordered]@{
            mode = "UNKNOWN"
            wpf_state = "MISSING"
            winui_state = "MISSING"
            winui_detail = "host publish report missing"
        }
    }

    $mode = Get-KeyValueFromFile -Path $HostReportPath -Prefix "HostPublishMode:"
    if ([string]::IsNullOrWhiteSpace($mode)) { $mode = "UNKNOWN" }

    $wpfSmoke = Get-KeyValueFromFile -Path $HostReportPath -Prefix "WPF launch smoke:"
    $wpfExe = Get-KeyValueFromFile -Path $HostReportPath -Prefix "WPF exe:"
    $wpfState = "UNKNOWN"
    if ($wpfSmoke -eq "PASS") {
        $wpfState = "PASS"
    } elseif ($wpfSmoke -match "^FAIL") {
        $wpfState = "FAIL"
    } elseif (-not [string]::IsNullOrWhiteSpace($wpfExe)) {
        $wpfState = "PASS"
    }

    $winUiPublish = Get-StatusFromFile -Path $HostReportPath -Pattern "WinUI publish"
    $winUiState = "UNKNOWN"
    if ($winUiPublish -match "skipped") {
        $winUiState = "SKIPPED"
    } elseif ($winUiPublish -match "failed") {
        $winUiState = "FAIL"
    } elseif (-not [string]::IsNullOrWhiteSpace((Get-KeyValueFromFile -Path $HostReportPath -Prefix "WinUI exe:"))) {
        $winUiState = "PASS"
    }

    return [ordered]@{
        mode = $mode
        wpf_state = $wpfState
        winui_state = $winUiState
        winui_detail = $winUiPublish
    }
}

$items = @(
    [PSCustomObject]@{ track = "VSFAvatar"; file = (Join-Path $ReportDir "vsfavatar_gate_summary.txt"); pattern = "- Overall:" },
    [PSCustomObject]@{ track = "VRM"; file = (Join-Path $ReportDir "vrm_gate_fixed5.txt"); pattern = "- Overall:" },
    [PSCustomObject]@{ track = "VXAvatar"; file = (Join-Path $ReportDir "vxavatar_gate_summary.txt"); pattern = "- Overall:" }
)

$rows = @()
foreach ($i in $items) {
    $rows += [PSCustomObject]@{
        track = $i.track
        status_line = Get-StatusFromFile -Path $i.file -Pattern $i.pattern
        source_file = $i.file
    }
}

$hostReport = Join-Path $ReportDir "host_publish_latest.txt"
$hostTrack = Get-HostTrackState -HostReportPath $hostReport
$rows += [PSCustomObject]@{
    track = "Host Publish (mode)"
    status_line = $hostTrack.mode
    source_file = $hostReport
}
$rows += [PSCustomObject]@{
    track = "Host Publish (WPF)"
    status_line = $hostTrack.wpf_state
    source_file = $hostReport
}
$rows += [PSCustomObject]@{
    track = "Host Publish (WinUI)"
    status_line = "$($hostTrack.winui_state): $($hostTrack.winui_detail)"
    source_file = $hostReport
}

$avatarRows = @($rows | Where-Object { $_.track -in @("VSFAvatar", "VRM", "VXAvatar") })
$avatarAllPass = $true
foreach ($r in $avatarRows) {
    if ((Get-PassFailFromStatusLine -Line $r.status_line) -ne "PASS") {
        $avatarAllPass = $false
        break
    }
}

$wpfReleaseCandidate = $avatarAllPass -and ($hostTrack.wpf_state -eq "PASS")
$fullReleaseCandidate = $wpfReleaseCandidate -and ($hostTrack.winui_state -eq "PASS")

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    gate_summary = [ordered]@{
        avatar_gates_all_pass = $avatarAllPass
        host_mode = $hostTrack.mode
        host_wpf_pass = ($hostTrack.wpf_state -eq "PASS")
        host_winui_pass = ($hostTrack.winui_state -eq "PASS")
        release_candidate_wpf_only = $wpfReleaseCandidate
        release_candidate_full = $fullReleaseCandidate
    }
    rows = $rows
}

$outDir = Split-Path -Parent $OutputJson
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$summary | ConvertTo-Json -Depth 5 | Set-Content -Path $OutputJson

$lines = @()
$lines += "Release Gate Dashboard"
$lines += "GeneratedUTC: $($summary.generated_utc)"
$lines += "ReleaseCandidateWpfOnly: $(if ($wpfReleaseCandidate) { 'PASS' } else { 'FAIL' })"
$lines += "ReleaseCandidateFull: $(if ($fullReleaseCandidate) { 'PASS' } else { 'FAIL' })"
$lines += ""
foreach ($r in $rows) {
    $lines += "- $($r.track): $($r.status_line)"
}
$lines | Set-Content -Path $OutputTxt

Write-Host "json=$OutputJson"
Write-Host "txt=$OutputTxt"
