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

$items = @(
    [PSCustomObject]@{ track = "VSFAvatar"; file = (Join-Path $ReportDir "vsfavatar_gate_summary.txt"); pattern = "Overall" },
    [PSCustomObject]@{ track = "VRM"; file = (Join-Path $ReportDir "vrm_gate_fixed5.txt"); pattern = "Overall" },
    [PSCustomObject]@{ track = "VXAvatar"; file = (Join-Path $ReportDir "vxavatar_gate_summary.txt"); pattern = "Overall" },
    [PSCustomObject]@{ track = "Host Publish"; file = (Join-Path $ReportDir "host_publish_latest.txt"); pattern = "WinUI publish" }
)

$rows = @()
foreach ($i in $items) {
    $rows += [PSCustomObject]@{
        track = $i.track
        status_line = Get-StatusFromFile -Path $i.file -Pattern $i.pattern
        source_file = $i.file
    }
}

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("s")
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
$lines += ""
foreach ($r in $rows) {
    $lines += "- $($r.track): $($r.status_line)"
}
$lines | Set-Content -Path $OutputTxt

Write-Host "json=$OutputJson"
Write-Host "txt=$OutputTxt"
