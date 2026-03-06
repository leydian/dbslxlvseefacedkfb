param(
    [string]$ReportDir = ".\build\reports",
    [string]$OutputJson = ".\build\reports\release_gate_dashboard.json",
    [string]$OutputTxt = ".\build\reports\release_gate_dashboard.txt",
    [switch]$RequireUnityXav2ForWpfOnly,
    [switch]$RequireUnityXav2ForFull = $true
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

$hostE2EGate = Join-Path $ReportDir "host_e2e_gate_summary.txt"
$rows += [PSCustomObject]@{
    track = "Tracking HostE2E"
    status_line = Get-StatusFromFile -Path $hostE2EGate -Pattern "Overall:"
    source_file = $hostE2EGate
}

$trackingFuzzGate = Join-Path $ReportDir "tracking_parser_fuzz_gate_summary.txt"
$rows += [PSCustomObject]@{
    track = "Tracking Parser Fuzz"
    status_line = Get-StatusFromFile -Path $trackingFuzzGate -Pattern "- Overall:"
    source_file = $trackingFuzzGate
}

$mediapipeSanity = Join-Path $ReportDir "mediapipe_sidecar_sanity_summary.txt"
$rows += [PSCustomObject]@{
    track = "Tracking Mediapipe Sanity"
    status_line = Get-StatusFromFile -Path $mediapipeSanity -Pattern "Overall:"
    source_file = $mediapipeSanity
}

$unityValidationSummary = Join-Path $ReportDir "unity_xav2_validation_summary.txt"
$unityStatus = Get-KeyValueFromFile -Path $unityValidationSummary -Prefix "overall_status="
if ([string]::IsNullOrWhiteSpace($unityStatus)) {
    $unityStatus = if (Test-Path $unityValidationSummary) { "UNKNOWN" } else { "NOT_RUN" }
}
$rows += [PSCustomObject]@{
    track = "Unity XAV2 Validate"
    status_line = $unityStatus
    source_file = $unityValidationSummary
}

$compressionGate = Join-Path $ReportDir "xav2_compression_quality_gate_summary.txt"
$rows += [PSCustomObject]@{
    track = "XAV2 Compression Quality"
    status_line = Get-StatusFromFile -Path $compressionGate -Pattern "- Overall:"
    source_file = $compressionGate
}

$parityGate = Join-Path $ReportDir "xav2_parity_gate_summary.txt"
$rows += [PSCustomObject]@{
    track = "XAV2 Unity/Native Parity"
    status_line = Get-StatusFromFile -Path $parityGate -Pattern "- Overall:"
    source_file = $parityGate
}

$renderPerfGate = Join-Path $ReportDir "render_perf_gate_summary.txt"
$rows += [PSCustomObject]@{
    track = "Render Perf (Overall)"
    status_line = Get-StatusFromFile -Path $renderPerfGate -Pattern "- Overall:"
    source_file = $renderPerfGate
}
$rows += [PSCustomObject]@{
    track = "Render Perf (P95 Private MB)"
    status_line = "P95PrivateMb: $(Get-KeyValueFromFile -Path $renderPerfGate -Prefix 'P95PrivateMb:')"
    source_file = $renderPerfGate
}
$rows += [PSCustomObject]@{
    track = "Render Perf (P95 WorkingSet MB)"
    status_line = "P95WorkingSetMb: $(Get-KeyValueFromFile -Path $renderPerfGate -Prefix 'P95WorkingSetMb:')"
    source_file = $renderPerfGate
}
$rows += [PSCustomObject]@{
    track = "Host Dist (WPF MB)"
    status_line = "WPFDistMb: $(Get-KeyValueFromFile -Path $hostReport -Prefix 'WPF dist size mb:')"
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

$unityPass = [string]::Equals($unityStatus, "PASS", [System.StringComparison]::OrdinalIgnoreCase)
$compressionPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "XAV2 Compression Quality" }) | Select-Object -First 1).status_line)) -eq "PASS"
$parityPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "XAV2 Unity/Native Parity" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingHostE2EPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "Tracking HostE2E" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingFuzzPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "Tracking Parser Fuzz" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingMediapipeSanityPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "Tracking Mediapipe Sanity" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingContractAllPass = $trackingHostE2EPass -and $trackingFuzzPass -and $trackingMediapipeSanityPass

$unityXav2AllPass = $unityPass -and $compressionPass -and $parityPass
$wpfUnityRequirementMet = if ($RequireUnityXav2ForWpfOnly) { $unityXav2AllPass } else { $true }
$fullUnityRequirementMet = if ($RequireUnityXav2ForFull) { $unityXav2AllPass } else { $true }

$wpfReleaseCandidate = $avatarAllPass -and ($hostTrack.wpf_state -eq "PASS") -and $wpfUnityRequirementMet -and $trackingContractAllPass
$fullReleaseCandidate = $avatarAllPass -and ($hostTrack.wpf_state -eq "PASS") -and ($hostTrack.winui_state -eq "PASS") -and $fullUnityRequirementMet -and $trackingContractAllPass

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    gate_summary = [ordered]@{
        avatar_gates_all_pass = $avatarAllPass
        unity_xav2_validate_pass = $unityPass
        xav2_compression_quality_pass = $compressionPass
        xav2_unity_native_parity_pass = $parityPass
        unity_xav2_all_pass = $unityXav2AllPass
        unity_xav2_required_wpf_only = [bool]$RequireUnityXav2ForWpfOnly
        unity_xav2_required_full = [bool]$RequireUnityXav2ForFull
        host_mode = $hostTrack.mode
        host_wpf_pass = ($hostTrack.wpf_state -eq "PASS")
        host_winui_pass = ($hostTrack.winui_state -eq "PASS")
        tracking_host_e2e_pass = $trackingHostE2EPass
        tracking_parser_fuzz_pass = $trackingFuzzPass
        tracking_mediapipe_sanity_pass = $trackingMediapipeSanityPass
        tracking_contract_all_pass = $trackingContractAllPass
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
$lines += "Policy.RequireUnityXav2ForWpfOnly: $RequireUnityXav2ForWpfOnly"
$lines += "Policy.RequireUnityXav2ForFull: $RequireUnityXav2ForFull"
$lines += "ReleaseCandidateWpfOnly: $(if ($wpfReleaseCandidate) { 'PASS' } else { 'FAIL' })"
$lines += "ReleaseCandidateFull: $(if ($fullReleaseCandidate) { 'PASS' } else { 'FAIL' })"
$lines += ""
foreach ($r in $rows) {
    $lines += "- $($r.track): $($r.status_line)"
}
$lines | Set-Content -Path $OutputTxt

Write-Host "json=$OutputJson"
Write-Host "txt=$OutputTxt"
