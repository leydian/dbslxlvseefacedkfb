param(
    [string]$SampleDir = "..\sample",
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$SidecarPath = ".\build\Release\vsfavatar_sidecar.exe",
    [string]$ReportScriptPath = ".\tools\vsfavatar_sample_report.ps1",
    [string]$ReportPath = ".\build\reports\vsfavatar_render_probe.txt",
    [string]$SummaryPath = ".\build\reports\vsfavatar_render_gate_summary.txt",
    [switch]$UseFixedSet,
    [string[]]$FixedSamples = @(
        "NewOnYou.vsfavatar",
        "Character vywjd.vsfavatar",
        "PPU (2).vsfavatar",
        "VRM dkdlrh.vsfavatar"
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
$sampleCount = @($lines | Where-Object { $_ -match '^----\s+.+\.vsfavatar$' }).Count
if ($sampleCount -eq 0) {
    throw "no sample headers parsed from report: $ReportPath"
}

$renderableMatches = @($lines | Where-Object { $_ -match '^\s+MeshPayloads:\s*([1-9]\d*)\s*$' })
$atLeastOneRenderable = $renderableMatches.Count -ge 1
$completeStageCount = @($lines | Where-Object { $_ -match '^\s+ParserStage:\s*complete\s*$' }).Count
$emptyPrimaryCount = @($lines | Where-Object { $_ -match '^\s+PrimaryError:\s*$' }).Count
$noEmptyPrimaryOnComplete = $completeStageCount -gt 0 -and $emptyPrimaryCount -eq 0
$overall = $atLeastOneRenderable -and $noEmptyPrimaryOnComplete

$summary = @()
$summary += "VSFAvatar Render Gate Summary"
$summary += "Generated: $(Get-Date -Format s)"
$summary += "ReportPath: $ReportPath"
$summary += "SampleCount: $sampleCount"
$summary += ""
$summary += "Gate Results"
$summary += "- GateR1 (at least one sample has mesh payloads): $(if($atLeastOneRenderable){'PASS'}else{'FAIL'})"
$summary += "- GateR2 (complete-stage rows have primary error code): $(if($noEmptyPrimaryOnComplete){'PASS'}else{'FAIL'})"
$summary += "- Overall: $(if($overall){'PASS'}else{'FAIL'})"
$summary += ""
$summary += "Metrics"
$summary += "- complete_stage_rows: $completeStageCount"
$summary += "- renderable_mesh_payload_rows: $($renderableMatches.Count)"
$summary += "- empty_primary_rows: $emptyPrimaryCount"

$summaryDir = Split-Path -Parent $SummaryPath
if (-not (Test-Path $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir | Out-Null
}
$summary | Set-Content -Path $SummaryPath -Encoding UTF8
Write-Host "Summary written: $SummaryPath"

if (-not $overall) {
    exit 1
}
