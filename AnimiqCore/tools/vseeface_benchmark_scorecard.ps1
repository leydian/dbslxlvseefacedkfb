param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$ObservationsPath,
    [string]$FocusProduct = "animiq",
    [string]$BaselineProduct = "vseeface",
    [string]$OutputJson = ".\build\reports\vseeface_benchmark_scorecard.json",
    [string]$OutputTxt = ".\build\reports\vseeface_benchmark_scorecard.txt",
    [string]$OutputMd = ".\build\reports\vseeface_benchmark_scorecard.md"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AbsPath {
    param([string]$PathValue, [string]$BaseDir = "")
    if ([string]::IsNullOrWhiteSpace($PathValue)) {
        return ""
    }
    if (-not [System.IO.Path]::IsPathRooted($PathValue) -and -not [string]::IsNullOrWhiteSpace($BaseDir)) {
        return [System.IO.Path]::GetFullPath((Join-Path $BaseDir $PathValue))
    }
    return [System.IO.Path]::GetFullPath($PathValue)
}

function Ensure-ParentDirectory {
    param([string]$FilePath)
    $dir = Split-Path -Parent $FilePath
    if (-not [string]::IsNullOrWhiteSpace($dir) -and -not (Test-Path $dir)) {
        New-Item -Path $dir -ItemType Directory -Force | Out-Null
    }
}

function Get-Avg {
    param([double[]]$Values)
    if ($null -eq $Values -or $Values.Count -eq 0) {
        return $null
    }
    return [double](($Values | Measure-Object -Average).Average)
}

function Round2 {
    param($Number)
    if ($null -eq $Number) {
        return $null
    }
    return [math]::Round([double]$Number, 2)
}

function Convert-ToScore5 {
    param(
        [double]$FocusValue,
        [double]$BaselineValue,
        [string]$Better
    )
    if ($FocusValue -le 0 -and $BaselineValue -le 0) {
        return 3.0
    }

    $ratio = 1.0
    if ($Better -eq "lower") {
        if ($FocusValue -gt 0) {
            $ratio = $BaselineValue / $FocusValue
        }
    } else {
        if ($BaselineValue -gt 0) {
            $ratio = $FocusValue / $BaselineValue
        }
    }

    if ($ratio -lt 0.5) { return 1.0 }
    if ($ratio -lt 0.8) { return 2.0 }
    if ($ratio -lt 1.05) { return 3.0 }
    if ($ratio -lt 1.2) { return 4.0 }
    return 5.0
}

function Get-NormalizedIndex {
    param(
        [double]$FocusValue,
        [double]$BaselineValue,
        [string]$Better
    )
    if ($null -eq $FocusValue -or $null -eq $BaselineValue -or $BaselineValue -eq 0) {
        return $null
    }
    if ($Better -eq "lower") {
        return 100.0 * ($BaselineValue / [math]::Max(0.0001, $FocusValue))
    }
    return 100.0 * ($FocusValue / $BaselineValue)
}

function Get-MetricNumericValue {
    param($Record)
    if ($null -ne $Record.avg) {
        return [double]$Record.avg
    }
    if ($null -ne $Record.value) {
        return [double]$Record.value
    }
    return $null
}

function Validate-Record {
    param($Record)
    $required = @(
        "test_id", "scenario", "tool_name", "tool_version",
        "hardware_profile", "quality_preset", "axis_id",
        "metric_name", "value", "unit", "sample_count",
        "avg", "p95", "worst", "notes", "pass_fail"
    )
    foreach ($key in $required) {
        $p = $Record.PSObject.Properties[$key]
        if ($null -eq $p -or $null -eq $p.Value -or [string]::IsNullOrWhiteSpace("$($p.Value)")) {
            throw "observations schema invalid: records[] missing required field '$key'"
        }
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$manifestAbs = Resolve-AbsPath -PathValue $ManifestPath -BaseDir $repoRoot
$observationsAbs = Resolve-AbsPath -PathValue $ObservationsPath -BaseDir $repoRoot
$outputJsonAbs = Resolve-AbsPath -PathValue $OutputJson -BaseDir $repoRoot
$outputTxtAbs = Resolve-AbsPath -PathValue $OutputTxt -BaseDir $repoRoot
$outputMdAbs = Resolve-AbsPath -PathValue $OutputMd -BaseDir $repoRoot

if (-not (Test-Path $manifestAbs)) {
    throw "ManifestPath not found: $manifestAbs"
}
if (-not (Test-Path $observationsAbs)) {
    throw "ObservationsPath not found: $observationsAbs"
}

$manifest = Get-Content -Path $manifestAbs -Raw | ConvertFrom-Json
$obs = Get-Content -Path $observationsAbs -Raw | ConvertFrom-Json
if ($null -eq $manifest.axes) {
    throw "manifest schema invalid: axes[] missing"
}
if ($null -eq $obs.records) {
    throw "observations schema invalid: records[] missing"
}

$records = @($obs.records)
if ($records.Count -eq 0) {
    throw "observations schema invalid: records[] is empty"
}

foreach ($r in $records) {
    Validate-Record -Record $r
}

$axisRows = [System.Collections.Generic.List[object]]::new()

foreach ($axis in $manifest.axes) {
    $axisId = "$($axis.id)"
    $axisName = "$($axis.name)"
    $axisWeight = [double]$axis.weight
    $kpis = @($axis.kpis)

    $axisRecords = @($records | Where-Object { "$($_.axis_id)" -eq $axisId })
    $focusRecords = @($axisRecords | Where-Object { "$($_.tool_name)".ToLowerInvariant() -eq $FocusProduct.ToLowerInvariant() })
    $baselineRecords = @($axisRecords | Where-Object { "$($_.tool_name)".ToLowerInvariant() -eq $BaselineProduct.ToLowerInvariant() })

    $metricRows = [System.Collections.Generic.List[object]]::new()
    $metricScores = @()
    $metricIndexes = @()
    foreach ($kpi in $kpis) {
        $metricKey = "$($kpi.key)"
        $better = "$($kpi.better)".ToLowerInvariant()
        $unit = "$($kpi.unit)"

        $focusVals = @(
            $focusRecords |
                Where-Object { "$($_.metric_name)" -eq $metricKey } |
                ForEach-Object { Get-MetricNumericValue -Record $_ } |
                Where-Object { $null -ne $_ }
        )
        $baselineVals = @(
            $baselineRecords |
                Where-Object { "$($_.metric_name)" -eq $metricKey } |
                ForEach-Object { Get-MetricNumericValue -Record $_ } |
                Where-Object { $null -ne $_ }
        )

        $focusAvg = Get-Avg -Values $focusVals
        $baselineAvg = Get-Avg -Values $baselineVals

        $score = $null
        $relativeIndex = $null
        if ($null -ne $focusAvg -and $null -ne $baselineAvg) {
            $score = Convert-ToScore5 -FocusValue $focusAvg -BaselineValue $baselineAvg -Better $better
            $relativeIndex = Get-NormalizedIndex -FocusValue $focusAvg -BaselineValue $baselineAvg -Better $better
            $metricScores += $score
            $metricIndexes += $relativeIndex
        }

        $focusPass = @($focusRecords | Where-Object { "$($_.metric_name)" -eq $metricKey -and "$($_.pass_fail)".ToLowerInvariant() -eq "pass" }).Count
        $focusTotal = @($focusRecords | Where-Object { "$($_.metric_name)" -eq $metricKey }).Count
        $baselinePass = @($baselineRecords | Where-Object { "$($_.metric_name)" -eq $metricKey -and "$($_.pass_fail)".ToLowerInvariant() -eq "pass" }).Count
        $baselineTotal = @($baselineRecords | Where-Object { "$($_.metric_name)" -eq $metricKey }).Count

        $metricRows.Add([PSCustomObject]@{
            metric_name = $metricKey
            better = $better
            unit = $unit
            focus_avg = Round2 $focusAvg
            baseline_avg = Round2 $baselineAvg
            score_1_to_5 = Round2 $score
            relative_index_baseline_100 = Round2 $relativeIndex
            focus_pass_rate = if ($focusTotal -gt 0) { Round2 ($focusPass / $focusTotal) } else { $null }
            baseline_pass_rate = if ($baselineTotal -gt 0) { Round2 ($baselinePass / $baselineTotal) } else { $null }
            focus_samples = $focusTotal
            baseline_samples = $baselineTotal
        })
    }

    $axisScore = Get-Avg -Values $metricScores
    $axisIndex = Get-Avg -Values $metricIndexes
    $axisGap = if ($null -ne $axisScore) { [double](3.0 - $axisScore) } else { $null }

    $rice = $axis.riceDefaults
    $riceScore = $null
    if ($null -ne $axisGap) {
        $reach = [double]$rice.reach
        $impact = [double]$rice.impact
        $confidence = [double]$rice.confidence
        $effort = [double]$rice.effort
        $riceScore = (($reach * $impact * ($confidence / 100.0)) / [math]::Max(1.0, $effort)) * [math]::Max(0.0, $axisGap)
    }

    $axisRows.Add([PSCustomObject]@{
        axis_id = $axisId
        axis_name = $axisName
        owner = "$($axis.owner)"
        weight = Round2 $axisWeight
        focus_product = $FocusProduct
        baseline_product = $BaselineProduct
        axis_score_1_to_5 = Round2 $axisScore
        axis_gap_from_neutral = Round2 $axisGap
        axis_relative_index_baseline_100 = Round2 $axisIndex
        rice_score = Round2 $riceScore
        metrics = @($metricRows)
    })
}

$weightedScore = 0.0
$weightedIndex = 0.0
$totalWeight = 0.0
foreach ($row in $axisRows) {
    if ($null -ne $row.axis_score_1_to_5) {
        $weightedScore += ([double]$row.axis_score_1_to_5 * [double]$row.weight)
        $totalWeight += [double]$row.weight
    }
    if ($null -ne $row.axis_relative_index_baseline_100) {
        $weightedIndex += ([double]$row.axis_relative_index_baseline_100 * [double]$row.weight)
    }
}

$overallScore = if ($totalWeight -gt 0) { $weightedScore / $totalWeight } else { $null }
$overallIndex = if ($totalWeight -gt 0) { $weightedIndex / $totalWeight } else { $null }

$priorityBacklog = @(
    $axisRows |
        Where-Object { $null -ne $_.rice_score -and $_.axis_gap_from_neutral -gt 0 } |
        Sort-Object rice_score -Descending |
        Select-Object axis_id, axis_name, owner, axis_score_1_to_5, axis_gap_from_neutral, rice_score
)

$summary = [PSCustomObject]@{
    benchmark_name = "$($manifest.benchmarkName)"
    generated_at = (Get-Date).ToString("o")
    focus_product = $FocusProduct
    baseline_product = $BaselineProduct
    overall_weighted_score_1_to_5 = Round2 $overallScore
    overall_relative_index_baseline_100 = Round2 $overallIndex
    axes = @($axisRows)
    priority_backlog = @($priorityBacklog)
}

Ensure-ParentDirectory -FilePath $outputJsonAbs
Ensure-ParentDirectory -FilePath $outputTxtAbs
Ensure-ParentDirectory -FilePath $outputMdAbs

$summary | ConvertTo-Json -Depth 10 | Set-Content -Path $outputJsonAbs -Encoding UTF8

$txtLines = @()
$txtLines += "VSeeFace 10-Axis Benchmark Scorecard"
$txtLines += "===================================="
$txtLines += "GeneratedAt: $($summary.generated_at)"
$txtLines += "FocusProduct: $FocusProduct"
$txtLines += "BaselineProduct: $BaselineProduct"
$txtLines += "OverallWeightedScore(1-5): $($summary.overall_weighted_score_1_to_5)"
$txtLines += "OverallRelativeIndex(baseline=100): $($summary.overall_relative_index_baseline_100)"
$txtLines += ""
$txtLines += "Axis Scores"
$txtLines += "-----------"
foreach ($row in $axisRows) {
    $txtLines += "{0} | score={1} | idx={2} | gap={3} | rice={4}" -f `
        $row.axis_id, $row.axis_score_1_to_5, $row.axis_relative_index_baseline_100, $row.axis_gap_from_neutral, $row.rice_score
}
$txtLines += ""
$txtLines += "Priority Backlog"
$txtLines += "----------------"
if ($priorityBacklog.Count -eq 0) {
    $txtLines += "no gaps found"
} else {
    foreach ($b in $priorityBacklog) {
        $txtLines += "{0} | rice={1} | score={2}" -f $b.axis_id, $b.rice_score, $b.axis_score_1_to_5
    }
}
$txtLines | Set-Content -Path $outputTxtAbs -Encoding UTF8

$mdLines = @()
$mdLines += "# VSeeFace 10-Axis Benchmark Scorecard"
$mdLines += ""
$mdLines += "- GeneratedAt: ``$($summary.generated_at)``"
$mdLines += "- FocusProduct: ``$FocusProduct``"
$mdLines += "- BaselineProduct: ``$BaselineProduct``"
$mdLines += "- OverallWeightedScore(1-5): **$($summary.overall_weighted_score_1_to_5)**"
$mdLines += "- OverallRelativeIndex(baseline=100): **$($summary.overall_relative_index_baseline_100)**"
$mdLines += ""
$mdLines += "## Axis Results"
$mdLines += "| Axis | Owner | Score(1-5) | RelativeIndex | Gap | RICE |"
$mdLines += "|---|---|---:|---:|---:|---:|"
foreach ($row in $axisRows) {
    $mdLines += "| $($row.axis_id) | $($row.owner) | $($row.axis_score_1_to_5) | $($row.axis_relative_index_baseline_100) | $($row.axis_gap_from_neutral) | $($row.rice_score) |"
}
$mdLines += ""
$mdLines += "## Priority Backlog"
$mdLines += "| Rank | Axis | Owner | Score | Gap | RICE |"
$mdLines += "|---:|---|---|---:|---:|---:|"
if ($priorityBacklog.Count -eq 0) {
    $mdLines += "| 1 | no gaps found | - | - | - | - |"
} else {
    $rank = 1
    foreach ($b in $priorityBacklog) {
        $mdLines += "| $rank | $($b.axis_id) | $($b.owner) | $($b.axis_score_1_to_5) | $($b.axis_gap_from_neutral) | $($b.rice_score) |"
        $rank++
    }
}
$mdLines | Set-Content -Path $outputMdAbs -Encoding UTF8

Write-Host ("[vseeface-benchmark] wrote: {0}" -f $outputJsonAbs)
Write-Host ("[vseeface-benchmark] wrote: {0}" -f $outputTxtAbs)
Write-Host ("[vseeface-benchmark] wrote: {0}" -f $outputMdAbs)
