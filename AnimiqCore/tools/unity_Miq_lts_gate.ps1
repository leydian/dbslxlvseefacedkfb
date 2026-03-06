param(
    [string]$MatrixPath = ".\tools\unity_lts_matrix.json",
    [string]$UnityProjectPath = $env:UNITY_MIQ_PROJECT_PATH,
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$ReportDir = ".\build\reports",
    [string]$OfficialLinesCsv = "2021-lts,2022-lts,2023-lts",
    [string]$CandidateLinesCsv = "",
    [switch]$IncludeCandidates,
    [int]$RecentSampleCount = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-Lines {
    param([string]$Csv)
    return @($Csv.Split(",") | ForEach-Object { $_.Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Invoke-Gate {
    param(
        [string]$Name,
        [string]$Command,
        [string]$SummaryPath
    )
    Write-Host "[unity_miq_lts_gate] START: $Name"
    Invoke-Expression $Command
    $exitCode = $LASTEXITCODE
    return [PSCustomObject]@{
        name = $Name
        command = $Command
        exit_code = $exitCode
        status = if ($exitCode -eq 0) { "PASS" } else { "FAIL" }
        summary = $SummaryPath
    }
}

function Write-LtsSummary {
    param(
        [Parameter(Mandatory = $true)]$Summary,
        [Parameter(Mandatory = $true)][string]$SummaryJsonPath,
        [Parameter(Mandatory = $true)][string]$SummaryTxtPath,
        [string[]]$ExtraArtifacts = @()
    )

    $Summary | ConvertTo-Json -Depth 12 | Set-Content -Path $SummaryJsonPath -Encoding UTF8

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("Unity MIQ LTS Gate Summary")
    $lines.Add("Generated: $($Summary.generated)")
    $lines.Add("OfficialLines: $($Summary.official_lines -join ', ')")
    $lines.Add("CandidateLines: $($Summary.candidate_lines -join ', ')")
    $lines.Add("IncludeCandidates: $([bool]$Summary.include_candidates)")
    $lines.Add("GatePolicy: $($Summary.gate_policy)")
    $lines.Add("Overall: $($Summary.overall)")
    $lines.Add("")

    if ($Summary.preflight -ne $null) {
        $lines.Add("Preflight")
        foreach ($p in $Summary.preflight) {
            $lines.Add("- $($p.line): $($p.status) (expected=$($p.expected_unity_version), env=$($p.editor_env_var), editorExists=$($p.editor_path_exists), projectExists=$($p.project_path_exists))")
        }
        $lines.Add("")
    }

    if ($Summary.line_status -ne $null) {
        $lines.Add("LineStatus")
        foreach ($s in $Summary.line_status) {
            $lines.Add("- $($s.line): overall=$($s.overall), validate=$($s.validate), parity=$($s.parity), compression=$($s.compression)")
        }
        $lines.Add("")
    }

    $lines.Add("Results")
    foreach ($row in $Summary.results) {
        $lines.Add("- $($row.name): $($row.status) (exit=$($row.exit_code))")
    }
    $lines.Add("")

    $lines.Add("Artifacts")
    $lines.Add("- summary_json=$SummaryJsonPath")
    $lines.Add("- summary_txt=$SummaryTxtPath")
    foreach ($artifact in $ExtraArtifacts) {
        $lines.Add("- $artifact")
    }

    $lines | Set-Content -Path $SummaryTxtPath -Encoding UTF8
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedMatrixPath = Resolve-AbsolutePath -Path $MatrixPath -BaseDirectory $repoRoot
if (-not (Test-Path -LiteralPath $resolvedMatrixPath)) {
    throw "Unity LTS matrix file not found: $resolvedMatrixPath"
}
if ([string]::IsNullOrWhiteSpace($UnityProjectPath)) {
    throw "Unity project path is required. Set -UnityProjectPath or UNITY_MIQ_PROJECT_PATH."
}
if (-not (Test-Path -LiteralPath $UnityProjectPath)) {
    throw "Unity project path not found: $UnityProjectPath"
}
if (-not (Test-Path -LiteralPath $AvatarToolPath)) {
    throw "avatar_tool not found: $AvatarToolPath"
}

Push-Location $repoRoot
try {
    & powershell -ExecutionPolicy Bypass -File .\tools\unity_project_lock_check.ps1 -UnityProjectPath $UnityProjectPath
    if ($LASTEXITCODE -ne 0) {
        throw "Unity project lock preflight failed. Resolve lock and retry."
    }
}
finally {
    Pop-Location
}

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$summaryJsonPath = Join-Path $ReportDir "unity_miq_lts_gate_summary.json"
$summaryTxtPath = Join-Path $ReportDir "unity_miq_lts_gate_summary.txt"
$historyCsvPath = Join-Path $ReportDir "unity_miq_lts_gate_history.csv"
$kpiJsonPath = Join-Path $ReportDir "unity_miq_lts_kpi_summary.json"
$kpiTxtPath = Join-Path $ReportDir "unity_miq_lts_kpi_summary.txt"

$matrix = Get-Content -Raw -Path $resolvedMatrixPath | ConvertFrom-Json
$officialLines = Get-Lines -Csv $OfficialLinesCsv
$candidateLines = Get-Lines -Csv $CandidateLinesCsv
$targetLines = @($officialLines)
if ($IncludeCandidates) {
    $targetLines += $candidateLines
}
$targetLines = @($targetLines | Select-Object -Unique)

if ($targetLines.Count -eq 0) {
    throw "No Unity lines selected."
}

$gatePolicy = "official-lines-all-pass-required"
$nowUtc = (Get-Date).ToUniversalTime().ToString("s")

$preflight = [System.Collections.Generic.List[object]]::new()
$preflightFailed = $false
foreach ($line in $targetLines) {
    $entry = $matrix.PSObject.Properties[$line].Value
    if ($null -eq $entry) {
        $preflight.Add([PSCustomObject]@{
            line = $line
            status = "FAIL"
            expected_unity_version = ""
            editor_env_var = ""
            editor_path = ""
            editor_path_exists = $false
            project_path = $UnityProjectPath
            project_path_exists = (Test-Path -LiteralPath $UnityProjectPath)
            detail = "line_not_found_in_matrix"
        })
        $preflightFailed = $true
        continue
    }

    $editorEnvVar = [string]$entry.editor_env_var
    $expectedVersion = [string]$entry.expected_unity_version
    $editorPath = [string][System.Environment]::GetEnvironmentVariable($editorEnvVar)
    $editorExists = (-not [string]::IsNullOrWhiteSpace($editorPath)) -and (Test-Path -LiteralPath $editorPath)
    $projectExists = Test-Path -LiteralPath $UnityProjectPath
    $status = if ($editorExists -and $projectExists) { "PASS" } else { "FAIL" }

    $preflight.Add([PSCustomObject]@{
        line = $line
        status = $status
        expected_unity_version = $expectedVersion
        editor_env_var = $editorEnvVar
        editor_path = $editorPath
        editor_path_exists = [bool]$editorExists
        project_path = $UnityProjectPath
        project_path_exists = [bool]$projectExists
        detail = if ($status -eq "PASS") { "ok" } else { "missing_editor_or_project_path" }
    })

    if ($status -ne "PASS") {
        $preflightFailed = $true
    }
}

if ($preflightFailed) {
    $summary = [ordered]@{
        generated = $nowUtc
        official_lines = $officialLines
        candidate_lines = $candidateLines
        include_candidates = [bool]$IncludeCandidates
        gate_policy = $gatePolicy
        overall = "FAIL"
        preflight = $preflight
        line_status = @()
        results = @()
    }
    Write-LtsSummary -Summary $summary -SummaryJsonPath $summaryJsonPath -SummaryTxtPath $summaryTxtPath
    exit 1
}

$results = [System.Collections.Generic.List[object]]::new()
foreach ($line in $targetLines) {
    $validateSummary = Join-Path $ReportDir "unity_miq_${line}_validation_summary.txt"
    $paritySummary = Join-Path $ReportDir "miq_parity_gate_${line}_summary.txt"
    $compressionSummary = Join-Path $ReportDir "miq_compression_quality_gate_${line}_summary.txt"

    $validateCmd = "powershell -ExecutionPolicy Bypass -File .\tools\unity_miq_validate.ps1 -UnityLine $line -MatrixPath `"$resolvedMatrixPath`" -UnityProjectPath `"$UnityProjectPath`" -ReportDir `"$ReportDir`" -ReportSuffix $line"
    $parityCmd = "powershell -ExecutionPolicy Bypass -File .\tools\miq_parity_gate.ps1 -UnityLine $line -MatrixPath `"$resolvedMatrixPath`" -UnityProjectPath `"$UnityProjectPath`" -AvatarToolPath `"$AvatarToolPath`" -ReportDir `"$ReportDir`" -ReportSuffix $line"
    $compressionCmd = "powershell -ExecutionPolicy Bypass -File .\tools\miq_compression_quality_gate.ps1 -UnityLine $line -MatrixPath `"$resolvedMatrixPath`" -UnityProjectPath `"$UnityProjectPath`" -ReportDir `"$ReportDir`" -ReportSuffix $line"

    $results.Add((Invoke-Gate -Name "validate:$line" -Command $validateCmd -SummaryPath $validateSummary))
    $results.Add((Invoke-Gate -Name "parity:$line" -Command $parityCmd -SummaryPath $paritySummary))
    $results.Add((Invoke-Gate -Name "compression:$line" -Command $compressionCmd -SummaryPath $compressionSummary))
}

$lineStatus = [System.Collections.Generic.List[object]]::new()
foreach ($line in $targetLines) {
    $validate = ($results | Where-Object { $_.name -eq "validate:$line" } | Select-Object -First 1)
    $parity = ($results | Where-Object { $_.name -eq "parity:$line" } | Select-Object -First 1)
    $compression = ($results | Where-Object { $_.name -eq "compression:$line" } | Select-Object -First 1)

    $lineOverall = if (($validate.status -eq "PASS") -and ($parity.status -eq "PASS") -and ($compression.status -eq "PASS")) {
        "PASS"
    } else {
        "FAIL"
    }

    $lineStatus.Add([PSCustomObject]@{
        line = $line
        validate = $validate.status
        parity = $parity.status
        compression = $compression.status
        overall = $lineOverall
    })
}

$officialFailures = $lineStatus | Where-Object { $_.line -in $officialLines -and $_.overall -ne "PASS" }
$overallOfficialPass = ($officialFailures.Count -eq 0)

$summary = [ordered]@{
    generated = $nowUtc
    official_lines = $officialLines
    candidate_lines = $candidateLines
    include_candidates = [bool]$IncludeCandidates
    gate_policy = $gatePolicy
    overall = if ($overallOfficialPass) { "PASS" } else { "FAIL" }
    preflight = $preflight
    line_status = $lineStatus
    results = $results
}

if (-not (Test-Path -LiteralPath $historyCsvPath)) {
    "generated_utc,unity_line,expected_unity_version,official_line,validate_status,parity_status,compression_status,line_overall,policy_overall" | Set-Content -Path $historyCsvPath -Encoding UTF8
}

foreach ($ls in $lineStatus) {
    $entry = $matrix.PSObject.Properties[$ls.line].Value
    $row = [PSCustomObject]@{
        generated_utc = $nowUtc
        unity_line = $ls.line
        expected_unity_version = [string]$entry.expected_unity_version
        official_line = [bool]($ls.line -in $officialLines)
        validate_status = $ls.validate
        parity_status = $ls.parity
        compression_status = $ls.compression
        line_overall = $ls.overall
        policy_overall = $summary.overall
    }
    $row | Export-Csv -Path $historyCsvPath -NoTypeInformation -Append
}

$historyRows = @()
if (Test-Path -LiteralPath $historyCsvPath) {
    $historyRows = Import-Csv -Path $historyCsvPath
}

$kpiRows = [System.Collections.Generic.List[object]]::new()
foreach ($line in $targetLines) {
    $lineRows = @($historyRows | Where-Object { $_.unity_line -eq $line })
    $lineTotal = $lineRows.Count
    $linePass = @($lineRows | Where-Object { $_.line_overall -eq "PASS" }).Count
    $lineRate = if ($lineTotal -gt 0) { [math]::Round((100.0 * $linePass / $lineTotal), 2) } else { 0.0 }

    $recentRows = @($lineRows | Sort-Object generated_utc -Descending | Select-Object -First $RecentSampleCount)
    $recentTotal = $recentRows.Count
    $recentPass = @($recentRows | Where-Object { $_.line_overall -eq "PASS" }).Count
    $recentRate = if ($recentTotal -gt 0) { [math]::Round((100.0 * $recentPass / $recentTotal), 2) } else { 0.0 }

    $kpiRows.Add([PSCustomObject]@{
        line = $line
        samples_total = $lineTotal
        pass_total = $linePass
        pass_rate_pct = $lineRate
        recent_samples = $recentTotal
        recent_pass = $recentPass
        recent_pass_rate_pct = $recentRate
    })
}

$kpiSummary = [ordered]@{
    generated = $nowUtc
    history_csv = $historyCsvPath
    recent_sample_count = $RecentSampleCount
    rows = $kpiRows
}
$kpiSummary | ConvertTo-Json -Depth 8 | Set-Content -Path $kpiJsonPath -Encoding UTF8

$kpiLines = [System.Collections.Generic.List[string]]::new()
$kpiLines.Add("Unity MIQ LTS KPI Summary")
$kpiLines.Add("Generated: $nowUtc")
$kpiLines.Add("HistoryCsv: $historyCsvPath")
$kpiLines.Add("RecentSampleCount: $RecentSampleCount")
$kpiLines.Add("")
foreach ($row in $kpiRows) {
    $kpiLines.Add("- $($row.line): total=$($row.samples_total), pass=$($row.pass_total), rate=$($row.pass_rate_pct)% | recent($RecentSampleCount)=$($row.recent_pass)/$($row.recent_samples) ($($row.recent_pass_rate_pct)%)")
}
$kpiLines | Set-Content -Path $kpiTxtPath -Encoding UTF8

Write-LtsSummary -Summary $summary -SummaryJsonPath $summaryJsonPath -SummaryTxtPath $summaryTxtPath -ExtraArtifacts @(
    "history_csv=$historyCsvPath",
    "kpi_json=$kpiJsonPath",
    "kpi_txt=$kpiTxtPath"
)

if (-not $overallOfficialPass) {
    exit 1
}
exit 0
