param(
    [string]$SampleDir = "..\sample",
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$SidecarPath = ".\build\Release\vsfavatar_sidecar.exe",
    [string]$ReportScriptPath = ".\tools\vsfavatar_sample_report.ps1",
    [string]$ReportPath = ".\build\reports\vsfavatar_render_probe.txt",
    [string]$SummaryPath = ".\build\reports\vsfavatar_render_gate_summary.txt",
    [string]$TargetSamplePattern = "*11-3.vsfavatar",
    [switch]$UseFixedSet,
    [string[]]$FixedSamples = @(
        "NewOnYou.vsfavatar",
        "Character vywjd.vsfavatar",
        "PPU (2).vsfavatar",
        "VRM dkdlrh.vsfavatar",
        "*11-3.vsfavatar"
    )
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ReportScriptPath)) {
    throw "report script not found: $ReportScriptPath"
}

if ($UseFixedSet) {
    & $ReportScriptPath -SampleDir $SampleDir -AvatarToolPath $AvatarToolPath -SidecarPath $SidecarPath -OutputPath $ReportPath -UseFixedSet -FixedSamples $FixedSamples
} else {
    & $ReportScriptPath -SampleDir $SampleDir -AvatarToolPath $AvatarToolPath -SidecarPath $SidecarPath -OutputPath $ReportPath
}

$lines = Get-Content -Path $ReportPath

$sampleRows = @()
$currentSample = $null
for ($i = 0; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    if ($line -match '^----\s+(.+\.vsfavatar)$') {
        $j = $i + 1
        while ($j -lt $lines.Count -and [string]::IsNullOrWhiteSpace($lines[$j])) {
            $j++
        }
        if ($j -ge $lines.Count -or (($lines[$j] -notlike 'Load *') -and ($lines[$j] -notlike '  Sidecar*'))) {
            continue
        }
        if ($null -ne $currentSample) {
            $sampleRows += $currentSample
        }
        $currentSample = [ordered]@{
            Name = $matches[1].Trim()
            ParserStage = ""
            PrimaryError = ""
            MeshPayloads = 0
            SidecarMeshExtractStage = ""
            SidecarRenderPayloadMode = ""
            SidecarTimingMs = ""
        }
        continue
    }
    if ($null -eq $currentSample) {
        continue
    }
    if ($line -match '^\s+ParserStage:\s*(.+)\s*$') {
        $currentSample.ParserStage = $matches[1].Trim()
        continue
    }
    if ($line -match '^\s+PrimaryError:\s*(.*)$') {
        $currentSample.PrimaryError = $matches[1].Trim()
        continue
    }
    if ($line -match '^\s+MeshPayloads:\s*(\d+)\s*$') {
        $currentSample.MeshPayloads = [int]$matches[1]
        continue
    }
    if ($line -match '^\s+SidecarMeshExtractStage:\s*(.*)$') {
        $currentSample.SidecarMeshExtractStage = $matches[1].Trim()
        continue
    }
    if ($line -match '^\s+SidecarRenderPayloadMode:\s*(.*)$') {
        $currentSample.SidecarRenderPayloadMode = $matches[1].Trim()
        continue
    }
    if ($line -match '^\s+SidecarTimingMs:\s*(.*)$') {
        $currentSample.SidecarTimingMs = $matches[1].Trim()
        continue
    }
}
if ($null -ne $currentSample) {
    $sampleRows += $currentSample
}
$sampleCount = $sampleRows.Count
if ($sampleCount -eq 0) {
    throw "no sample headers parsed from report: $ReportPath"
}

$renderableMatches = @($lines | Where-Object { $_ -match '^\s+MeshPayloads:\s*([1-9]\d*)\s*$' })
$atLeastOneRenderable = $renderableMatches.Count -ge 1
$completeStageCount = @($lines | Where-Object { $_ -match '^\s+ParserStage:\s*complete\s*$' }).Count
$emptyPrimaryCount = @($lines | Where-Object { $_ -match '^\s+PrimaryError:\s*$' }).Count
$noEmptyPrimaryOnComplete = $completeStageCount -gt 0 -and $emptyPrimaryCount -eq 0
$targetRow = $sampleRows | Where-Object { $_.Name -like $TargetSamplePattern } | Select-Object -First 1
$targetSamplePresent = $null -ne $targetRow
$targetHasContractFields = $targetSamplePresent -and
    (-not [string]::IsNullOrWhiteSpace("$($targetRow.SidecarMeshExtractStage)")) -and
    (-not [string]::IsNullOrWhiteSpace("$($targetRow.SidecarTimingMs)"))
$previewPassRows = @(
    $sampleRows | Where-Object {
        $_.ParserStage -eq "complete" -and (
            $_.MeshPayloads -gt 0 -or
            $_.SidecarRenderPayloadMode -eq "placeholder_quad_v1"
        )
    }
)
$outputPassRows = @(
    $sampleRows | Where-Object {
        $_.ParserStage -eq "complete" -and $_.MeshPayloads -gt 0 -and $_.SidecarRenderPayloadMode -ne "placeholder_quad_v1"
    }
)
$placeholderDependentRows = @(
    $sampleRows | Where-Object {
        $_.ParserStage -eq "complete" -and $_.MeshPayloads -gt 0 -and $_.SidecarRenderPayloadMode -eq "placeholder_quad_v1"
    }
)
$outputReadiness = if ($outputPassRows.Count -gt 0) { "PASS" } else { "FAIL" }
$placeholderDependencyStatus = if ($placeholderDependentRows.Count -gt 0) { "YES" } else { "NO" }
$overall = $atLeastOneRenderable -and $noEmptyPrimaryOnComplete -and $targetSamplePresent -and $targetHasContractFields

$summary = @()
$summary += "VSFAvatar Render Gate Summary"
$summary += "Generated: $(Get-Date -Format s)"
$summary += "ReportPath: $ReportPath"
$summary += "SampleCount: $sampleCount"
$summary += "TargetSamplePattern: $TargetSamplePattern"
$summary += ""
$summary += "Gate Results"
$summary += "- GateR1 (at least one sample has mesh payloads): $(if($atLeastOneRenderable){'PASS'}else{'FAIL'})"
$summary += "- GateR2 (complete-stage rows have primary error code): $(if($noEmptyPrimaryOnComplete){'PASS'}else{'FAIL'})"
$summary += "- GateR3 (target sample row present): $(if($targetSamplePresent){'PASS'}else{'FAIL'})"
$summary += "- GateR4 (target row has sidecar contract fields): $(if($targetHasContractFields){'PASS'}else{'FAIL'})"
$summary += "- Overall: $(if($overall){'PASS'}else{'FAIL'})"
$summary += ""
$summary += "Metrics"
$summary += "- complete_stage_rows: $completeStageCount"
$summary += "- renderable_mesh_payload_rows: $($renderableMatches.Count)"
$summary += "- preview_pass_rows: $($previewPassRows.Count)"
$summary += "- output_pass_rows: $($outputPassRows.Count)"
$summary += "- placeholder_dependent_rows: $($placeholderDependentRows.Count)"
$summary += "- output_readiness: $outputReadiness"
$summary += "- placeholder_dependency: $placeholderDependencyStatus"
$summary += "- empty_primary_rows: $emptyPrimaryCount"
$summary += "- parsed_sample_rows: $($sampleRows.Count)"
if ($targetSamplePresent) {
    $summary += "- target_stage: $($targetRow.ParserStage)"
    $summary += "- target_primary_error: $($targetRow.PrimaryError)"
    $summary += "- target_mesh_payloads: $($targetRow.MeshPayloads)"
    $summary += "- target_mesh_extract_stage: $($targetRow.SidecarMeshExtractStage)"
    $summary += "- target_render_payload_mode: $($targetRow.SidecarRenderPayloadMode)"
    $summary += "- target_timing_ms: $($targetRow.SidecarTimingMs)"
} else {
    $summary += "- target_stage: n/a"
    $summary += "- target_primary_error: n/a"
    $summary += "- target_mesh_payloads: n/a"
    $summary += "- target_mesh_extract_stage: n/a"
    $summary += "- target_render_payload_mode: n/a"
    $summary += "- target_timing_ms: n/a"
}

$summaryDir = Split-Path -Parent $SummaryPath
if (-not (Test-Path $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir | Out-Null
}
$summary | Set-Content -Path $SummaryPath -Encoding UTF8
Write-Host "Summary written: $SummaryPath"

if (-not $overall) {
    exit 1
}
