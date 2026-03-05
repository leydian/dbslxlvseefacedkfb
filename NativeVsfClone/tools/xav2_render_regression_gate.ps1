param(
    [string]$SampleDir = ".",
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$SummaryPath = ".\build\reports\xav2_render_regression_gate_summary.txt",
    [string]$TargetSamplePattern = "*11-3.xav2",
    [switch]$FailOnRenderWarnings
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $AvatarToolPath)) {
    throw "avatar_tool not found: $AvatarToolPath"
}
if (-not (Test-Path $SampleDir)) {
    throw "sample dir not found: $SampleDir"
}

$files = Get-ChildItem -Path $SampleDir -Filter *.xav2
if ($files.Count -eq 0) {
    throw "no .xav2 files found under: $SampleDir"
}

$rows = @()
foreach ($f in $files) {
    $out = & $AvatarToolPath $f.FullName
    $row = [ordered]@{
        Name = $f.Name
        Compat = ""
        ParserStage = ""
        PrimaryError = ""
        WarningCount = 0
        WarningCodes = ""
    }
    foreach ($line in $out) {
        if ($line -match '^\s*Compat:\s*(.+)$') { $row.Compat = $matches[1].Trim() }
        elseif ($line -match '^\s*ParserStage:\s*(.+)$') { $row.ParserStage = $matches[1].Trim() }
        elseif ($line -match '^\s*PrimaryError:\s*(.+)$') { $row.PrimaryError = $matches[1].Trim() }
        elseif ($line -match '^\s*WarningCodes:\s*(\d+)$') { $row.WarningCount = [int]$matches[1] }
        elseif ($line -match '^\s*LastWarningCode:\s*(.+)$') { $row.WarningCodes = $matches[1].Trim() }
    }
    $rows += [PSCustomObject]$row
}

$target = $rows | Where-Object { $_.Name -like $TargetSamplePattern } | Select-Object -First 1
$gate1 = $true
foreach ($r in $rows) {
    if (-not "$($r.ParserStage)".ToLowerInvariant().Contains("runtime-ready")) {
        $gate1 = $false
        break
    }
}
$gate2 = $true
foreach ($r in $rows) {
    if (-not "$($r.PrimaryError)".ToUpperInvariant().Contains("NONE")) {
        $gate2 = $false
        break
    }
}
$gate3 = $null -ne $target
$gate4 = $true
if ($FailOnRenderWarnings) {
    foreach ($r in $rows) {
        if ($r.WarningCodes -eq "XAV2_SKINNING_STATIC_DISABLED" -or
            $r.WarningCodes -eq "XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED") {
            $gate4 = $false
            break
        }
    }
}

$overall = $gate1 -and $gate2 -and $gate3 -and $gate4

$summary = @()
$summary += "XAV2 Render Regression Gate Summary"
$summary += "Generated: $(Get-Date -Format s)"
$summary += "SampleDir: $(Resolve-Path $SampleDir)"
$summary += "SampleCount: $($rows.Count)"
$summary += ""
$summary += "Gate Results"
$summary += "- GateX1 (all parser stage runtime-ready): $(if($gate1){'PASS'}else{'FAIL'})"
$summary += "- GateX2 (all primary error NONE): $(if($gate2){'PASS'}else{'FAIL'})"
$summary += "- GateX3 (target sample present): $(if($gate3){'PASS'}else{'FAIL'})"
$summary += "- GateX4 (no critical render warnings when strict): $(if($gate4){'PASS'}else{'FAIL'})"
$summary += "- Overall: $(if($overall){'PASS'}else{'FAIL'})"
$summary += ""
$summary += "Rows"
foreach ($r in $rows) {
    $summary += "- $($r.Name): compat=$($r.Compat), stage=$($r.ParserStage), error=$($r.PrimaryError), last_warning_code=$($r.WarningCodes)"
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
