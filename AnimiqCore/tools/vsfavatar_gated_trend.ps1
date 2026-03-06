param(
    [string]$ReportDir = ".\build\reports",
    [string]$Pattern = "vsfavatar_gate_summary*.txt",
    [int]$MaxFiles = 30,
    [string]$OutputTxt = ".\build\reports\vsfavatar_gate_trend_latest.txt",
    [string]$OutputJson = ".\build\reports\vsfavatar_gate_trend_latest.json"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Get-StatusValue {
    param([string[]]$Lines, [string]$Prefix)
    $line = $Lines | Where-Object { $_.StartsWith($Prefix) } | Select-Object -First 1
    if ($null -eq $line) { return "" }
    return $line.Substring($Prefix.Length).Trim()
}

function Parse-GeneratedTimestamp {
    param([string[]]$Lines, [string]$FallbackName)
    $generatedText = Get-StatusValue -Lines $Lines -Prefix "Generated:"
    if ([string]::IsNullOrWhiteSpace($generatedText)) {
        return [PSCustomObject]@{
            parsed = [DateTimeOffset]::MinValue
            text = ""
            fallback = $FallbackName
        }
    }

    $dto = [DateTimeOffset]::MinValue
    if ([DateTimeOffset]::TryParse($generatedText, [ref]$dto)) {
        return [PSCustomObject]@{
            parsed = $dto
            text = $generatedText
            fallback = ""
        }
    }

    return [PSCustomObject]@{
        parsed = [DateTimeOffset]::MinValue
        text = $generatedText
        fallback = $FallbackName
    }
}

function To-DoubleOrZero {
    param([string]$Value)
    $out = 0.0
    if ([double]::TryParse($Value, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$out)) {
        return $out
    }
    return 0.0
}

function To-IntOrZero {
    param([string]$Value)
    $out = 0
    if ([int]::TryParse($Value, [ref]$out)) {
        return $out
    }
    return 0
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedReportDir = Resolve-AbsolutePath -Path $ReportDir -BaseDirectory $repoRoot
$resolvedTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
$resolvedJson = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot

if (-not (Test-Path $resolvedReportDir)) {
    throw "report dir not found: $resolvedReportDir"
}

$files = Get-ChildItem -Path $resolvedReportDir -File -Filter $Pattern | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First $MaxFiles
if ($files.Count -eq 0) {
    throw "no summary files matched pattern '$Pattern' in: $resolvedReportDir"
}

$rows = @()
foreach ($file in $files) {
    $lines = Get-Content -Path $file.FullName
    $generated = Parse-GeneratedTimestamp -Lines $lines -FallbackName $file.Name
    $gateDLine = Get-StatusValue -Lines $lines -Prefix "- GateD (>=1 sample reaches complete + object_table_parsed=true + no primary error):"
    $overall = Get-StatusValue -Lines $lines -Prefix "- Overall:"
    $objTrue = To-IntOrZero (Get-StatusValue -Lines $lines -Prefix "- ObjectTableParsed_True:")
    $objFalse = To-IntOrZero (Get-StatusValue -Lines $lines -Prefix "- ObjectTableParsed_False:")
    $attemptAvg = To-DoubleOrZero (Get-StatusValue -Lines $lines -Prefix "- SerializedAttempts_Avg:")
    $attemptMax = To-DoubleOrZero (Get-StatusValue -Lines $lines -Prefix "- SerializedAttempts_Max:")

    $rows += [PSCustomObject]@{
        file_name = $file.Name
        path = $file.FullName
        generated = $generated.text
        gate_d_status = if ($gateDLine -match "PASS") { "PASS" } elseif ($gateDLine -match "FAIL") { "FAIL" } else { "UNKNOWN" }
        overall_status = if ($overall -match "PASS") { "PASS" } elseif ($overall -match "FAIL") { "FAIL" } else { "UNKNOWN" }
        object_table_parsed_true = $objTrue
        object_table_parsed_false = $objFalse
        serialized_attempts_avg = $attemptAvg
        serialized_attempts_max = $attemptMax
        sort_key = $generated.parsed.UtcDateTime.Ticks
    }
}

$ordered = $rows | Sort-Object sort_key
$gateDPassCount = @($ordered | Where-Object { $_.gate_d_status -eq "PASS" }).Count
$gateDFailCount = @($ordered | Where-Object { $_.gate_d_status -eq "FAIL" }).Count
$overallPassCount = @($ordered | Where-Object { $_.overall_status -eq "PASS" }).Count
$latest = $ordered | Select-Object -Last 1

$summaryObj = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
    sample_count = @($ordered).Count
    gate_d_pass_count = $gateDPassCount
    gate_d_fail_count = $gateDFailCount
    overall_pass_count = $overallPassCount
    latest_file = $latest.file_name
    latest_gate_d_status = $latest.gate_d_status
    latest_overall_status = $latest.overall_status
    rows = @($ordered | Select-Object file_name, generated, gate_d_status, overall_status, object_table_parsed_true, object_table_parsed_false, serialized_attempts_avg, serialized_attempts_max)
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTxt) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedJson) | Out-Null

$linesOut = [System.Collections.Generic.List[string]]::new()
$linesOut.Add("VSFAvatar Gate Trend Summary")
$linesOut.Add("GeneratedUTC: $($summaryObj.generated_utc)")
$linesOut.Add("SampleCount: $($summaryObj.sample_count)")
$linesOut.Add("GateDPassCount: $($summaryObj.gate_d_pass_count)")
$linesOut.Add("GateDFailCount: $($summaryObj.gate_d_fail_count)")
$linesOut.Add("OverallPassCount: $($summaryObj.overall_pass_count)")
$linesOut.Add("LatestFile: $($summaryObj.latest_file)")
$linesOut.Add("LatestGateD: $($summaryObj.latest_gate_d_status)")
$linesOut.Add("LatestOverall: $($summaryObj.latest_overall_status)")
$linesOut.Add("")
$linesOut.Add("Rows:")
foreach ($r in $summaryObj.rows) {
    $linesOut.Add("- $($r.file_name): generated=$($r.generated), gateD=$($r.gate_d_status), overall=$($r.overall_status), object_true=$($r.object_table_parsed_true), object_false=$($r.object_table_parsed_false), attempts_avg=$($r.serialized_attempts_avg), attempts_max=$($r.serialized_attempts_max)")
}

$linesOut | Set-Content -Path $resolvedTxt -Encoding UTF8
$summaryObj | ConvertTo-Json -Depth 5 | Set-Content -Path $resolvedJson -Encoding UTF8

Write-Host "txt=$resolvedTxt"
Write-Host "json=$resolvedJson"
