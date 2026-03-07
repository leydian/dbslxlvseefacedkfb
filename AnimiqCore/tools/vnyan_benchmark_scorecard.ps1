param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$ObservationsPath,
    [string]$FocusProduct = "animiq",
    [string]$BaselineProduct = "vnyan",
    [string]$OutputJson = ".\build\reports\vnyan_benchmark_scorecard.json",
    [string]$OutputTxt = ".\build\reports\vnyan_benchmark_scorecard.txt",
    [string]$OutputMd = ".\build\reports\vnyan_benchmark_scorecard.md"
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
if ($null -eq $obs.captures) {
    throw "observations schema invalid: captures[] missing"
}

$captures = @($obs.captures)
$axisRows = [System.Collections.Generic.List[object]]::new()

foreach ($axis in $manifest.axes) {
    $axisId = "$($axis.id)"
    $axisName = "$($axis.name)"
    $axisWeight = [double]$axis.weight
    $kpis = @($axis.kpis)

    $axisCaptures = @($captures | Where-Object { "$($_.axisId)" -eq $axisId })
    $focusCaptures = @($axisCaptures | Where-Object { "$($_.product)" -eq $FocusProduct })
    $baselineCaptures = @($axisCaptures | Where-Object { "$($_.product)" -eq $BaselineProduct })

    $metricRows = [System.Collections.Generic.List[object]]::new()
    $metricScores = @()
    foreach ($kpi in $kpis) {
        $key = "$($kpi.key)"
        $better = "$($kpi.better)".ToLowerInvariant()

        $focusVals = @(
            $focusCaptures |
                ForEach-Object {
                    $p = $_.kpis.PSObject.Properties[$key]
                    if ($null -ne $p -and $null -ne $p.Value) { [double]$p.Value }
                }
        )
        $baselineVals = @(
            $baselineCaptures |
                ForEach-Object {
                    $p = $_.kpis.PSObject.Properties[$key]
                    if ($null -ne $p -and $null -ne $p.Value) { [double]$p.Value }
                }
        )

        $focusAvg = Get-Avg -Values $focusVals
        $baselineAvg = Get-Avg -Values $baselineVals
        $score = $null
        if ($null -ne $focusAvg -and $null -ne $baselineAvg) {
            $score = Convert-ToScore5 -FocusValue $focusAvg -BaselineValue $baselineAvg -Better $better
            $metricScores += $score
        }

        $metricRows.Add([PSCustomObject]@{
            key = $key
            better = $better
            focus_avg = Round2 $focusAvg
            baseline_avg = Round2 $baselineAvg
            score_1_to_5 = Round2 $score
        })
    }

    $axisScore = Get-Avg -Values $metricScores
    $axisGap = $null
    if ($null -ne $axisScore) {
        $axisGap = [double](3.0 - $axisScore)
    }

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
        focus_samples = $focusCaptures.Count
        baseline_samples = $baselineCaptures.Count
        axis_score_1_to_5 = Round2 $axisScore
        axis_gap_from_neutral = Round2 $axisGap
        rice_score = Round2 $riceScore
        metrics = @($metricRows)
    })
}

$weightedScore = 0.0
$totalWeight = 0.0
foreach ($row in $axisRows) {
    if ($null -ne $row.axis_score_1_to_5) {
        $weightedScore += ([double]$row.axis_score_1_to_5 * [double]$row.weight)
        $totalWeight += [double]$row.weight
    }
}
$overall = if ($totalWeight -gt 0) { $weightedScore / $totalWeight } else { $null }

$priorityBacklog = @(
    $axisRows |
        Where-Object { $null -ne $_.rice_score -and $_.axis_gap_from_neutral -gt 0 } |
        Sort-Object rice_score -Descending |
        Select-Object `
            axis_id,
            axis_name,
            owner,
            axis_score_1_to_5,
            axis_gap_from_neutral,
            rice_score
)

$summary = [PSCustomObject]@{
    benchmark_name = "$($manifest.benchmarkName)"
    generated_at = (Get-Date).ToString("o")
    focus_product = $FocusProduct
    baseline_product = $BaselineProduct
    overall_weighted_score_1_to_5 = Round2 $overall
    axes = @($axisRows)
    priority_backlog = @($priorityBacklog)
}

Ensure-ParentDirectory -FilePath $outputJsonAbs
Ensure-ParentDirectory -FilePath $outputTxtAbs
Ensure-ParentDirectory -FilePath $outputMdAbs

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $outputJsonAbs -Encoding UTF8

$txtLines = @()
$txtLines += "VNyan 10-Axis Benchmark Scorecard"
$txtLines += "================================="
$txtLines += "GeneratedAt: $($summary.generated_at)"
$txtLines += "FocusProduct: $FocusProduct"
$txtLines += "BaselineProduct: $BaselineProduct"
$txtLines += "OverallWeightedScore(1-5): $($summary.overall_weighted_score_1_to_5)"
$txtLines += ""
$txtLines += "Axis Scores"
$txtLines += "-----------"
foreach ($row in $axisRows) {
    $txtLines += "{0} | score={1} | gap={2} | rice={3} | samples={4}/{5}" -f `
        $row.axis_id, $row.axis_score_1_to_5, $row.axis_gap_from_neutral, $row.rice_score, $row.focus_samples, $row.baseline_samples
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
$mdLines += "# VNyan 10-Axis Benchmark Scorecard"
$mdLines += ""
$mdLines += "- GeneratedAt: ``$($summary.generated_at)``"
$mdLines += "- FocusProduct: ``$FocusProduct``"
$mdLines += "- BaselineProduct: ``$BaselineProduct``"
$mdLines += "- OverallWeightedScore(1-5): **$($summary.overall_weighted_score_1_to_5)**"
$mdLines += ""
$mdLines += "## Axis Results"
$mdLines += "| Axis | Owner | Score(1-5) | Gap | RICE | Samples (focus/baseline) |"
$mdLines += "|---|---|---:|---:|---:|---:|"
foreach ($row in $axisRows) {
    $mdLines += "| $($row.axis_id) | $($row.owner) | $($row.axis_score_1_to_5) | $($row.axis_gap_from_neutral) | $($row.rice_score) | $($row.focus_samples)/$($row.baseline_samples) |"
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

Write-Host ("[vnyan-benchmark] wrote: {0}" -f $outputJsonAbs)
Write-Host ("[vnyan-benchmark] wrote: {0}" -f $outputTxtAbs)
Write-Host ("[vnyan-benchmark] wrote: {0}" -f $outputMdAbs)
