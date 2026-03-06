param(
    [string]$ReportDir = ".\build\reports",
    [string]$OutputJson = ".\build\reports\release_gate_dashboard.json",
    [string]$OutputTxt = ".\build\reports\release_gate_dashboard.txt",
    [switch]$RequireUnityXav2ForWpfOnly,
    [switch]$RequireUnityXav2ForFull = $true,
    [switch]$RequireOnboardingKpiForWpfOnly,
    [switch]$RequireOnboardingKpiForFull = $true,
    [double]$OnboardingWithin3MinSuccessRateThresholdPct = 70.0,
    [int]$OnboardingMinSessionCount = 5,
    [string]$PolicyVersion = "2026-03-06.1"
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

function Get-OnboardingKpiState {
    param(
        [string]$Path,
        [double]$ThresholdPct,
        [int]$MinSessionCount
    )

    if (-not (Test-Path $Path)) {
        return [ordered]@{
            status = "MISSING"
            detail = "onboarding KPI summary missing"
            session_count = 0
            within_3min_success_rate_pct = 0.0
            threshold_pct = $ThresholdPct
            min_session_count = $MinSessionCount
            pass = $false
        }
    }

    try {
        $json = Get-Content -Raw -Path $Path | ConvertFrom-Json
    } catch {
        return [ordered]@{
            status = "INVALID"
            detail = "onboarding KPI summary parse failed"
            session_count = 0
            within_3min_success_rate_pct = 0.0
            threshold_pct = $ThresholdPct
            min_session_count = $MinSessionCount
            pass = $false
        }
    }

    $sessionCount = 0
    $successRate = 0.0
    if ($null -ne $json.session_count) {
        $sessionCount = [int]$json.session_count
    }
    if ($null -ne $json.within_3min_success_rate_pct) {
        $successRate = [double]$json.within_3min_success_rate_pct
    }

    $hasEnoughSamples = $sessionCount -ge $MinSessionCount
    $ratePass = $successRate -ge $ThresholdPct
    $pass = $hasEnoughSamples -and $ratePass
    $status = if ($pass) { "PASS" } elseif (-not $hasEnoughSamples) { "INSUFFICIENT_SAMPLES" } else { "FAIL" }
    $detail = "sessions=$sessionCount, success_rate_pct=$successRate, threshold_pct=$ThresholdPct, min_sessions=$MinSessionCount"

    return [ordered]@{
        status = $status
        detail = $detail
        session_count = $sessionCount
        within_3min_success_rate_pct = $successRate
        threshold_pct = $ThresholdPct
        min_session_count = $MinSessionCount
        pass = $pass
    }
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

$unityLtsGateJson = Join-Path $ReportDir "unity_xav2_lts_gate_summary.json"
$unityLtsSummary = $null
if (Test-Path $unityLtsGateJson) {
    try {
        $unityLtsSummary = Get-Content -Raw -Path $unityLtsGateJson | ConvertFrom-Json
    } catch {
        $unityLtsSummary = $null
    }
}
$unityLtsKpiJson = Join-Path $ReportDir "unity_xav2_lts_kpi_summary.json"
$unityLtsKpiSummary = $null
if (Test-Path $unityLtsKpiJson) {
    try {
        $unityLtsKpiSummary = Get-Content -Raw -Path $unityLtsKpiJson | ConvertFrom-Json
    } catch {
        $unityLtsKpiSummary = $null
    }
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

$unityLtsGate = Join-Path $ReportDir "unity_xav2_lts_gate_summary.txt"
$unityLtsOverallLine = if ($unityLtsSummary -ne $null) { [string]$unityLtsSummary.overall } else { Get-StatusFromFile -Path $unityLtsGate -Pattern "Overall:" }
$rows += [PSCustomObject]@{
    track = "Unity XAV2 LTS Gate (Overall)"
    status_line = $unityLtsOverallLine
    source_file = $unityLtsGate
}
if ($unityLtsSummary -ne $null -and $unityLtsSummary.line_status -ne $null) {
    foreach ($lineRow in $unityLtsSummary.line_status) {
        $rows += [PSCustomObject]@{
            track = "Unity XAV2 LTS Line [$($lineRow.line)]"
            status_line = "overall=$($lineRow.overall), validate=$($lineRow.validate), parity=$($lineRow.parity), compression=$($lineRow.compression)"
            source_file = $unityLtsGateJson
        }
    }
}
if ($unityLtsKpiSummary -ne $null -and $unityLtsKpiSummary.rows -ne $null) {
    foreach ($kpiRow in $unityLtsKpiSummary.rows) {
        $rows += [PSCustomObject]@{
            track = "Unity XAV2 LTS KPI [$($kpiRow.line)]"
            status_line = "recent_rate=$($kpiRow.recent_pass_rate_pct)%, total_rate=$($kpiRow.pass_rate_pct)%, samples=$($kpiRow.samples_total)"
            source_file = $unityLtsKpiJson
        }
    }
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
    track = "Render Perf (Live Tick Samples)"
    status_line = "LiveTickSamples: $(Get-KeyValueFromFile -Path $renderPerfGate -Prefix 'LiveTickSamples:')"
    source_file = $renderPerfGate
}
$rows += [PSCustomObject]@{
    track = "Render Perf (Memory Sample Failures)"
    status_line = "MemorySampleFailedCount: $(Get-KeyValueFromFile -Path $renderPerfGate -Prefix 'MemorySampleFailedCount:')"
    source_file = $renderPerfGate
}
$rows += [PSCustomObject]@{
    track = "Host Dist (WPF MB)"
    status_line = "WPFDistMb: $(Get-KeyValueFromFile -Path $hostReport -Prefix 'WPF dist size mb:')"
    source_file = $hostReport
}

$releaseReadinessSummary = Join-Path $ReportDir "release_readiness_gate_summary.txt"
$rows += [PSCustomObject]@{
    track = "Release Readiness Policy (RenderPerfProfile)"
    status_line = Get-KeyValueFromFile -Path $releaseReadinessSummary -Prefix "RenderPerfProfile:"
    source_file = $releaseReadinessSummary
}
$rows += [PSCustomObject]@{
    track = "Release Readiness Policy (SoakMinSuccessRatio)"
    status_line = Get-KeyValueFromFile -Path $releaseReadinessSummary -Prefix "SoakMinSuccessRatio:"
    source_file = $releaseReadinessSummary
}

$onboardingKpiPath = Join-Path $ReportDir "onboarding_kpi_summary.json"
$onboardingKpiState = Get-OnboardingKpiState `
    -Path $onboardingKpiPath `
    -ThresholdPct $OnboardingWithin3MinSuccessRateThresholdPct `
    -MinSessionCount $OnboardingMinSessionCount

$rows += [PSCustomObject]@{
    track = "Onboarding KPI Gate"
    status_line = "$($onboardingKpiState.status): $($onboardingKpiState.detail)"
    source_file = $onboardingKpiPath
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
$unityLtsState = Get-PassFailFromStatusLine -Line $unityLtsOverallLine
$unityLtsPass = $unityLtsState -eq "PASS"
$compressionPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "XAV2 Compression Quality" }) | Select-Object -First 1).status_line)) -eq "PASS"
$parityPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "XAV2 Unity/Native Parity" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingHostE2EPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "Tracking HostE2E" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingFuzzPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "Tracking Parser Fuzz" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingMediapipeSanityPass = (Get-PassFailFromStatusLine -Line ((($rows | Where-Object { $_.track -eq "Tracking Mediapipe Sanity" }) | Select-Object -First 1).status_line)) -eq "PASS"
$trackingContractAllPass = $trackingHostE2EPass -and $trackingFuzzPass -and $trackingMediapipeSanityPass

$unityXav2AllPass = switch ($unityLtsState) {
    "PASS" { $true }
    "FAIL" { $false }
    default { $unityPass -and $compressionPass -and $parityPass }
}

$unityLtsRecentRisk = $false
$unityLtsRecentRiskLines = @()
if ($unityLtsKpiSummary -ne $null -and $unityLtsKpiSummary.rows -ne $null) {
    $officialLines = @()
    if ($unityLtsSummary -ne $null -and $unityLtsSummary.official_lines -ne $null) {
        $officialLines = @($unityLtsSummary.official_lines)
    } elseif ($unityLtsKpiSummary.rows -ne $null) {
        $officialLines = @($unityLtsKpiSummary.rows | ForEach-Object { $_.line })
    }
    foreach ($line in $officialLines) {
        $row = ($unityLtsKpiSummary.rows | Where-Object { $_.line -eq $line } | Select-Object -First 1)
        if ($null -eq $row) { continue }
        $recentRate = [double]$row.recent_pass_rate_pct
        if ($recentRate -lt 100.0) {
            $unityLtsRecentRisk = $true
            $unityLtsRecentRiskLines += "${line}:$recentRate"
        }
    }
}

$wpfUnityRequirementMet = if ($RequireUnityXav2ForWpfOnly) { $unityXav2AllPass } else { $true }
$fullUnityRequirementMet = if ($RequireUnityXav2ForFull) { $unityXav2AllPass } else { $true }
$wpfOnboardingRequirementMet = if ($RequireOnboardingKpiForWpfOnly) { [bool]$onboardingKpiState.pass } else { $true }
$fullOnboardingRequirementMet = if ($RequireOnboardingKpiForFull) { [bool]$onboardingKpiState.pass } else { $true }

$wpfReleaseCandidate = $avatarAllPass -and ($hostTrack.wpf_state -eq "PASS") -and $wpfUnityRequirementMet -and $trackingContractAllPass -and $wpfOnboardingRequirementMet
$fullReleaseCandidate = $avatarAllPass -and ($hostTrack.wpf_state -eq "PASS") -and ($hostTrack.winui_state -eq "PASS") -and $fullUnityRequirementMet -and $trackingContractAllPass -and $fullOnboardingRequirementMet

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    gate_summary = [ordered]@{
        policy_version = $PolicyVersion
        avatar_gates_all_pass = $avatarAllPass
        unity_xav2_validate_pass = $unityPass
        unity_xav2_lts_gate_pass = $unityLtsPass
        unity_xav2_lts_recent_risk = $unityLtsRecentRisk
        unity_xav2_lts_recent_risk_lines = $unityLtsRecentRiskLines
        xav2_compression_quality_pass = $compressionPass
        xav2_unity_native_parity_pass = $parityPass
        unity_xav2_all_pass = $unityXav2AllPass
        unity_xav2_required_wpf_only = [bool]$RequireUnityXav2ForWpfOnly
        unity_xav2_required_full = [bool]$RequireUnityXav2ForFull
        onboarding_kpi_path = $onboardingKpiPath
        onboarding_kpi_status = $onboardingKpiState.status
        onboarding_kpi_pass = [bool]$onboardingKpiState.pass
        onboarding_kpi_session_count = [int]$onboardingKpiState.session_count
        onboarding_within_3min_success_rate_pct = [double]$onboardingKpiState.within_3min_success_rate_pct
        onboarding_kpi_threshold_pct = [double]$onboardingKpiState.threshold_pct
        onboarding_kpi_min_session_count = [int]$onboardingKpiState.min_session_count
        onboarding_kpi_required_wpf_only = [bool]$RequireOnboardingKpiForWpfOnly
        onboarding_kpi_required_full = [bool]$RequireOnboardingKpiForFull
        host_mode = $hostTrack.mode
        host_wpf_pass = ($hostTrack.wpf_state -eq "PASS")
        host_winui_pass = ($hostTrack.winui_state -eq "PASS")
        tracking_host_e2e_pass = $trackingHostE2EPass
        tracking_parser_fuzz_pass = $trackingFuzzPass
        tracking_mediapipe_sanity_pass = $trackingMediapipeSanityPass
        tracking_contract_all_pass = $trackingContractAllPass
        release_candidate_tracking = $trackingContractAllPass
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
$lines += "Policy.Version: $PolicyVersion"
$lines += "Policy.RequireUnityXav2ForWpfOnly: $RequireUnityXav2ForWpfOnly"
$lines += "Policy.RequireUnityXav2ForFull: $RequireUnityXav2ForFull"
$lines += "Policy.RequireOnboardingKpiForWpfOnly: $RequireOnboardingKpiForWpfOnly"
$lines += "Policy.RequireOnboardingKpiForFull: $RequireOnboardingKpiForFull"
$lines += "Policy.OnboardingWithin3MinSuccessRateThresholdPct: $OnboardingWithin3MinSuccessRateThresholdPct"
$lines += "Policy.OnboardingMinSessionCount: $OnboardingMinSessionCount"
$lines += "OnboardingKpiStatus: $($onboardingKpiState.status)"
$lines += "OnboardingKpiDetail: $($onboardingKpiState.detail)"
$lines += "UnityXav2LtsRecentRisk: $(if ($unityLtsRecentRisk) { 'YES' } else { 'NO' })"
$lines += "UnityXav2LtsRecentRiskLines: $(if ($unityLtsRecentRiskLines.Count -gt 0) { $unityLtsRecentRiskLines -join ', ' } else { '<none>' })"
$lines += "TrackingContractCandidate: $(if ($trackingContractAllPass) { 'PASS' } else { 'FAIL' })"
$lines += "ReleaseCandidateWpfOnly: $(if ($wpfReleaseCandidate) { 'PASS' } else { 'FAIL' })"
$lines += "ReleaseCandidateFull: $(if ($fullReleaseCandidate) { 'PASS' } else { 'FAIL' })"
$lines += ""
foreach ($r in $rows) {
    $lines += "- $($r.track): $($r.status_line)"
}
$lines | Set-Content -Path $OutputTxt

Write-Host "json=$OutputJson"
Write-Host "txt=$OutputTxt"
