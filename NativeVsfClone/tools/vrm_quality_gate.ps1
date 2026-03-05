param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$ReportPath = "",
    [string]$SummaryPath = "",
    [ValidateSet("fixed5", "auto5")][string]$Profile = "fixed5",
    [switch]$UseFixedSet,
    [string[]]$FixedSamples = @(
        "Kikyo_FT Variant(Clone).vrm",
        "Kikyo_FT Variant.vrm",
        "Kikyo_FT Variant111.vrm",
        "Kikyo_FT Variant112.vrm",
        "NewOnYou.vrm"
    )
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $AvatarToolPath)) {
    throw "avatar_tool not found at $AvatarToolPath"
}
if (-not (Test-Path $SampleDir)) {
    throw "sample directory not found at $SampleDir"
}

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = if ($Profile -eq "fixed5") { ".\\build\\reports\\vrm_probe_fixed5.txt" } else { ".\\build\\reports\\vrm_probe_auto5.txt" }
}
if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
    $SummaryPath = if ($Profile -eq "fixed5") { ".\\build\\reports\\vrm_gate_fixed5.txt" } else { ".\\build\\reports\\vrm_gate_auto5.txt" }
}

function To-Int {
    param([string]$Value)
    [int]$out = 0
    if ([int]::TryParse($Value, [ref]$out)) {
        return $out
    }
    return 0
}

$reportDir = Split-Path -Parent $ReportPath
if (-not (Test-Path $reportDir)) {
    New-Item -ItemType Directory -Path $reportDir | Out-Null
}
$summaryDir = Split-Path -Parent $SummaryPath
if (-not (Test-Path $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir | Out-Null
}

$candidates = @()
if (($UseFixedSet -or $Profile -eq "fixed5") -and $FixedSamples.Count -gt 0) {
    foreach ($name in $FixedSamples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $candidates += Get-Item $p
        } else {
            throw "fixed sample missing: $name"
        }
    }
} else {
    $candidates = Get-ChildItem -Path $SampleDir -Filter *.vrm -File | Sort-Object Name | Select-Object -First 5
}

if ($candidates.Count -lt 5) {
    throw "at least 5 .vrm samples are required (found=$($candidates.Count))"
}

"VRM probe report" | Set-Content -Path $ReportPath
"Generated: $(Get-Date -Format s)" | Add-Content -Path $ReportPath
"SampleDir: $(Resolve-Path $SampleDir)" | Add-Content -Path $ReportPath
"Profile: $Profile" | Add-Content -Path $ReportPath
"UseFixedSet: $UseFixedSet" | Add-Content -Path $ReportPath
"FileCount: $($candidates.Count)" | Add-Content -Path $ReportPath
"" | Add-Content -Path $ReportPath

$rows = @()
foreach ($f in $candidates) {
    "---- $($f.Name)" | Add-Content -Path $ReportPath
    $raw = & $AvatarToolPath $f.FullName
    $raw | Add-Content -Path $ReportPath
    "" | Add-Content -Path $ReportPath

    $fields = @{}
    foreach ($line in $raw) {
        if ($line -match '^\s*([^:]+):\s*(.*)$') {
            $fields[$matches[1].Trim()] = $matches[2].Trim()
        }
    }
    $rows += [PSCustomObject]@{
        Name = $f.Name
        LoadSucceeded = ($raw -contains "Load succeeded")
        Format = "$($fields["Format"])"
        Compat = "$($fields["Compat"])"
        ParserStage = "$($fields["ParserStage"])"
        PrimaryError = "$($fields["PrimaryError"])"
        MeshPayloads = (To-Int "$($fields["MeshPayloads"])")
        MaterialPayloads = (To-Int "$($fields["MaterialPayloads"])")
        TexturePayloads = (To-Int "$($fields["TexturePayloads"])")
        ExpressionCount = (To-Int "$($fields["ExpressionCount"])")
        MaterialDiagnostics = (To-Int "$($fields["MaterialDiagnostics"])")
        BlendMaterials = (To-Int "$($fields["BlendMaterials"])")
    }
}

$gateA = $true  # basic load stability
$gateB = $true  # vrm/runtime-ready/mesh
$gateC = $true  # material/texture minimum
$gateD = $true  # expression extraction visibility
$gateE = $true  # expression bind visibility
$gateF = $true  # springbone metadata visibility
$gateG = $true  # blend material coverage (informational when no blend samples)
$gateH = $true  # spring payload completeness
$gateI = $true  # runtime activation readiness (payload/collider linkage)
$gateJ = $true  # spring payload stability guards
$gateGMode = "enforced"
$failReasons = @()

foreach ($r in $rows) {
    if (-not $r.LoadSucceeded) {
        $gateA = $false
        $failReasons += "GateA: $($r.Name) did not load successfully"
    }
    if ($r.Format -ne "VRM" -or $r.ParserStage -ne "runtime-ready" -or $r.MeshPayloads -le 0 -or $r.Compat -eq "failed") {
        $gateB = $false
        $failReasons += "GateB: $($r.Name) expected VRM/runtime-ready/mesh>0/non-failed but got format=$($r.Format), stage=$($r.ParserStage), mesh=$($r.MeshPayloads), compat=$($r.Compat)"
    }
    if ($r.MaterialPayloads -le 0 -or $r.TexturePayloads -le 0 -or $r.MaterialDiagnostics -le 0) {
        $gateC = $false
        $failReasons += "GateC: $($r.Name) expected material+texture payloads and material diagnostics > 0 but got material=$($r.MaterialPayloads), texture=$($r.TexturePayloads), materialDiag=$($r.MaterialDiagnostics)"
    }
    if ($r.ExpressionCount -le 0) {
        $gateD = $false
        $failReasons += "GateD: $($r.Name) expected ExpressionCount > 0 but got $($r.ExpressionCount)"
    }
}

$rows2 = @()
foreach ($f in $candidates) {
    $rawSample = & $AvatarToolPath $f.FullName
    $fields = @{}
    foreach ($line in $rawSample) {
        if ($line -match '^\s*([^:]+):\s*(.*)$') {
            $fields[$matches[1].Trim()] = $matches[2].Trim()
        }
    }
    $rows2 += [PSCustomObject]@{
        Name = $f.Name
        ExpressionBindTotal = (To-Int "$($fields["ExpressionBindTotal"])")
        SpringBonePresent = "$($fields["SpringBonePresent"])"
        SpringPayloads = (To-Int "$($fields["SpringPayloads"])")
        PhysicsColliders = (To-Int "$($fields["PhysicsColliders"])")
    }
}
foreach ($r2 in $rows2) {
    if ([string]::IsNullOrWhiteSpace($r2.SpringBonePresent) -or ($r2.SpringBonePresent -ne "true" -and $r2.SpringBonePresent -ne "false")) {
        $gateF = $false
        $failReasons += "GateF: $($r2.Name) expected SpringBonePresent to be true/false but got '$($r2.SpringBonePresent)'"
    }
    if ($r2.SpringPayloads -gt 0 -and $r2.SpringPayloads -gt 256) {
        $gateJ = $false
        $failReasons += "GateJ: $($r2.Name) spring payload count exceeds sanity limit (256): $($r2.SpringPayloads)"
    }
}
$hasSpringPayload = ($rows2 | Where-Object { $_.SpringPayloads -gt 0 } | Measure-Object).Count -gt 0
if (-not $hasSpringPayload) {
    $gateH = $false
    $failReasons += "GateH: expected at least one sample with SpringPayloads > 0"
}
$hasSpringAndCollider = ($rows2 | Where-Object { $_.SpringPayloads -gt 0 -and $_.PhysicsColliders -gt 0 } | Measure-Object).Count -gt 0
if ($hasSpringPayload -and -not $hasSpringAndCollider) {
    $gateI = $false
    $failReasons += "GateI: expected at least one spring-enabled sample with PhysicsColliders > 0"
}
$hasBoundExpression = ($rows2 | Where-Object { $_.ExpressionBindTotal -gt 0 } | Measure-Object).Count -gt 0
if (-not $hasBoundExpression) {
    $gateE = $false
    $failReasons += "GateE: expected at least one sample with ExpressionBindTotal > 0"
}
$hasBlendMaterial = ($rows | Where-Object { $_.BlendMaterials -gt 0 } | Measure-Object).Count -gt 0
if (-not $hasBlendMaterial) {
    $gateG = $true
    $gateGMode = "no-blend-sample"
}

$summary = @()
$summary += "VRM Quality Gate Summary"
$summary += "Generated: $(Get-Date -Format s)"
$summary += "ReportPath: $ReportPath"
$summary += "Profile: $Profile"
$summary += "UseFixedSet: $UseFixedSet"
$summary += ""
$summary += "Gate Results"
$summary += "- GateA (load stability): $(if($gateA){'PASS'}else{'FAIL'})"
$summary += "- GateB (VRM runtime-ready + mesh payload): $(if($gateB){'PASS'}else{'FAIL'})"
$summary += "- GateC (material+texture payload minimum): $(if($gateC){'PASS'}else{'FAIL'})"
$summary += "- GateD (expression count visibility): $(if($gateD){'PASS'}else{'FAIL'})"
$summary += "- GateE (expression bind visibility): $(if($gateE){'PASS'}else{'FAIL'})"
$summary += "- GateF (springbone metadata visibility): $(if($gateF){'PASS'}else{'FAIL'})"
$summary += "- GateG (blend material coverage): $(if($gateG){'PASS'}else{'FAIL'}) [mode=$gateGMode]"
$summary += "- GateH (spring payload completeness): $(if($gateH){'PASS'}else{'FAIL'})"
$summary += "- GateI (spring runtime activation readiness): $(if($gateI){'PASS'}else{'FAIL'})"
$summary += "- GateJ (spring payload stability guard): $(if($gateJ){'PASS'}else{'FAIL'})"
$overall = $gateA -and $gateB -and $gateC -and $gateD -and $gateE -and $gateF -and $gateH -and $gateI -and $gateJ
$summary += "- Overall: $(if($overall){'PASS'}else{'FAIL'})"
$summary += ""
$summary += "Per-sample"
foreach ($r in $rows) {
    $summary += ("- {0}: format={1}, compat={2}, stage={3}, primary={4}, mesh={5}, material={6}, texture={7}, expression={8}, materialDiag={9}, blendMat={10}" -f
        $r.Name, $r.Format, $r.Compat, $r.ParserStage, $r.PrimaryError, $r.MeshPayloads, $r.MaterialPayloads, $r.TexturePayloads, $r.ExpressionCount, $r.MaterialDiagnostics, $r.BlendMaterials)
}
if ($failReasons.Count -gt 0) {
    $summary += ""
    $summary += "Failure Reasons"
    foreach ($reason in $failReasons) {
        $summary += "- $reason"
    }
}

$summary | Set-Content -Path $SummaryPath
$summary | ForEach-Object { Write-Host $_ }
Write-Host "Report written: $ReportPath"

if ($overall) {
    exit 0
}
exit 1
