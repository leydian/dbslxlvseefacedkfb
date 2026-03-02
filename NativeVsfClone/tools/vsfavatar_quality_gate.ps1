param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$SidecarPath = ".\\build\\Release\\vsfavatar_sidecar.exe",
    [string]$ReportScriptPath = ".\\tools\\vsfavatar_sample_report.ps1",
    [string]$ReportPath = ".\\build\\reports\\vsfavatar_probe_latest_after_gate.txt",
    [string]$BaselinePath = ".\\build\\reports\\vsfavatar_probe_fixed.txt",
    [string]$SummaryPath = ".\\build\\reports\\vsfavatar_gate_summary.txt",
    [switch]$UseFixedSet,
    [string[]]$FixedSamples = @(
        "NewOnYou.vsfavatar",
        "Character vywjd.vsfavatar",
        "PPU (2).vsfavatar",
        "VRM dkdlrh.vsfavatar"
    )
)

$ErrorActionPreference = "Stop"

function Parse-Report {
    param(
        [Parameter(Mandatory = $true)][string]$Path
    )

    if (-not (Test-Path $Path)) {
        throw "report not found: $Path"
    }

    $lines = Get-Content -Path $Path
    $result = @{
        Header = @{}
        Samples = @{}
        Order = @()
    }

    $currentName = $null
    foreach ($line in $lines) {
        if ($line -match '^([^:]+):\s*(.*)$' -and $null -eq $currentName) {
            $result.Header[$matches[1].Trim()] = $matches[2].Trim()
            continue
        }
        if ($line -match '^----\s+(.+)$') {
            $currentName = $matches[1].Trim()
            if (-not $result.Samples.ContainsKey($currentName)) {
                $result.Samples[$currentName] = @{}
                $result.Order += $currentName
            }
            continue
        }
        if ($null -ne $currentName) {
            if ($line -match '^\s+([^:]+):\s*(.*)$') {
                $key = $matches[1].Trim()
                $value = $matches[2].Trim()
                $result.Samples[$currentName][$key] = $value
            } elseif ($line -match '^$') {
                $currentName = $null
            }
        }
    }
    return $result
}

function To-UInt64 {
    param([string]$Value)
    [UInt64]$out = 0
    if ([UInt64]::TryParse($Value, [ref]$out)) {
        return $out
    }
    return [UInt64]0
}

function Require-Field {
    param(
        [hashtable]$Sample,
        [string]$Field
    )
    return $Sample.ContainsKey($Field) -and -not [string]::IsNullOrWhiteSpace($Sample[$Field])
}

function Get-DiffLabel {
    param(
        [hashtable]$Current,
        [hashtable]$Baseline
    )
    if ($null -eq $Baseline) {
        return "NEW"
    }
    if ($Current["SidecarProbeStage"] -ne $Baseline["SidecarProbeStage"]) {
        if ($Current["SidecarProbeStage"] -eq "failed-serialized" -or $Current["SidecarProbeStage"] -eq "complete") {
            return "IMPROVED"
        }
        return "CHANGED"
    }
    if ($Current["SidecarPrimaryError"] -ne $Baseline["SidecarPrimaryError"]) {
        if ($Current["SidecarPrimaryError"] -eq "NONE") {
            return "IMPROVED"
        }
        return "CHANGED"
    }
    if ($Current["SidecarBestCandidateScore"] -ne $Baseline["SidecarBestCandidateScore"]) {
        if ((To-UInt64 $Current["SidecarBestCandidateScore"]) -gt (To-UInt64 $Baseline["SidecarBestCandidateScore"])) {
            return "IMPROVED"
        }
        return "REGRESSED"
    }
    if ($Current["SidecarReconCandidateCount"] -ne $Baseline["SidecarReconCandidateCount"]) {
        return "CHANGED"
    }
    if ($Current["SidecarFailedReadOffset"] -ne $Baseline["SidecarFailedReadOffset"] -or
        $Current["SidecarFailedCompressedSize"] -ne $Baseline["SidecarFailedCompressedSize"] -or
        $Current["SidecarFailedUncompressedSize"] -ne $Baseline["SidecarFailedUncompressedSize"]) {
        return "CHANGED"
    }
    return "UNCHANGED"
}

if (-not (Test-Path $ReportScriptPath)) {
    throw "report script not found: $ReportScriptPath"
}
if (-not (Test-Path $BaselinePath)) {
    throw "baseline report not found: $BaselinePath"
}

if ($UseFixedSet) {
    & $ReportScriptPath `
        -SampleDir $SampleDir `
        -AvatarToolPath $AvatarToolPath `
        -SidecarPath $SidecarPath `
        -OutputPath $ReportPath `
        -UseFixedSet `
        -FixedSamples $FixedSamples
} else {
    & $ReportScriptPath `
        -SampleDir $SampleDir `
        -AvatarToolPath $AvatarToolPath `
        -SidecarPath $SidecarPath `
        -OutputPath $ReportPath
}

$current = Parse-Report -Path $ReportPath
$baseline = Parse-Report -Path $BaselinePath

$requiredFields = @(
    "SidecarProbeStage",
    "SidecarPrimaryError",
    "SidecarObjectTableParsed",
    "SidecarOffsetFamily",
    "SidecarReconCandidateCount",
    "SidecarBestCandidateScore",
    "SidecarFailedReadOffset",
    "SidecarFailedCompressedSize",
    "SidecarFailedUncompressedSize"
)

$sampleNames = $current.Order
$gateA = $true
$gateB = $false
$gateC = $true
$gateD = $false
$failReasons = @()

if ($UseFixedSet -and $sampleNames.Count -ne $FixedSamples.Count) {
    $gateA = $false
    $failReasons += "GateA: fixed set sample count mismatch (expected=$($FixedSamples.Count), actual=$($sampleNames.Count))"
}
if ($current.Header.ContainsKey("FileCount")) {
    $headerFileCount = [int](To-UInt64 $current.Header["FileCount"])
    if ($headerFileCount -ne $sampleNames.Count) {
        $gateA = $false
        $failReasons += "GateA: header FileCount mismatch (expected=$headerFileCount, actual=$($sampleNames.Count))"
    }
}
if ($current.Header.ContainsKey("GateRows")) {
    $headerGateRows = [int](To-UInt64 $current.Header["GateRows"])
    if ($headerGateRows -ne $sampleNames.Count) {
        $gateA = $false
        $failReasons += "GateA: header GateRows mismatch (rows=$headerGateRows, samples=$($sampleNames.Count))"
    }
}

foreach ($name in $sampleNames) {
    $sample = $current.Samples[$name]

    if ($sample.ContainsKey("SidecarParseError")) {
        $gateA = $false
        $failReasons += "GateA: $name sidecar parse error"
    }

    foreach ($field in $requiredFields) {
        if (-not (Require-Field -Sample $sample -Field $field)) {
            $gateA = $false
            $failReasons += "GateA: $name missing field '$field'"
        }
    }

    $stage = $sample["SidecarProbeStage"]
    if ($stage -eq "failed-serialized" -or $stage -eq "complete") {
        $gateB = $true
    }
    $objectTableParsed = $sample["SidecarObjectTableParsed"]
    $primary = $sample["SidecarPrimaryError"]
    if ($stage -eq "complete" -and $objectTableParsed -eq "True" -and
        ([string]::IsNullOrWhiteSpace($primary) -or $primary -eq "NONE")) {
        $gateD = $true
    } elseif ($stage -ne "complete" -or $objectTableParsed -ne "True" -or
        (-not [string]::IsNullOrWhiteSpace($primary) -and $primary -ne "NONE")) {
        $failReasons += "GateD: $name unmet (stage=$stage, object_table_parsed=$objectTableParsed, primary=$primary)"
    }

    if ($primary -eq "DATA_BLOCK_READ_FAILED") {
        $offset = To-UInt64 $sample["SidecarFailedReadOffset"]
        $csize = To-UInt64 $sample["SidecarFailedCompressedSize"]
        $usize = To-UInt64 $sample["SidecarFailedUncompressedSize"]
        $family = $sample["SidecarOffsetFamily"]
        if ($offset -eq 0 -or $csize -eq 0 -or $usize -eq 0 -or [string]::IsNullOrWhiteSpace($family)) {
            $gateC = $false
            $failReasons += "GateC: $name missing failure tuple evidence (offset/csize/usize/family)"
        }
    }
}
if (-not $gateD) {
    $failReasons += "GateD: no sample reached complete with object_table_parsed=true and no primary error"
}

$gateAStatus = if ($gateA) { "PASS" } else { "FAIL" }
$gateBStatus = if ($gateB) { "PASS" } else { "FAIL" }
$gateCStatus = if ($gateC) { "PASS" } else { "FAIL" }
$gateDStatus = if ($gateD) { "PASS" } else { "FAIL" }
$overallPass = $gateA -and $gateB -and $gateC -and $gateD

$improved = 0
$regressed = 0
$unchanged = 0
$changed = 0
$newCount = 0
$diffLines = @()

foreach ($name in $sampleNames) {
    $cur = $current.Samples[$name]
    $base = $null
    if ($baseline.Samples.ContainsKey($name)) {
        $base = $baseline.Samples[$name]
    }
    $label = Get-DiffLabel -Current $cur -Baseline $base
    switch ($label) {
        "IMPROVED" { $improved++ }
        "REGRESSED" { $regressed++ }
        "UNCHANGED" { $unchanged++ }
        "CHANGED" { $changed++ }
        "NEW" { $newCount++ }
    }
    $diffLines += ("- {0}: {1} (stage={2}, primary={3}, family={4}, score={5})" -f
        $name, $label, $cur["SidecarProbeStage"], $cur["SidecarPrimaryError"], $cur["SidecarOffsetFamily"], $cur["SidecarBestCandidateScore"])
}

$summary = @()
$summary += "VSFAvatar Quality Gate Summary"
$summary += "Generated: $(Get-Date -Format s)"
$summary += "ReportPath: $ReportPath"
$summary += "BaselinePath: $BaselinePath"
$summary += "UseFixedSet: $UseFixedSet"
$summary += ""
$summary += "Gate Results"
$summary += "- GateA (stability + required fields): $gateAStatus"
$summary += "- GateB (>=1 sample reaches failed-serialized|complete): $gateBStatus"
$summary += "- GateC (DATA_BLOCK_READ_FAILED tuple evidence): $gateCStatus"
$summary += "- GateD (>=1 sample reaches complete + object_table_parsed=true + no primary error): $gateDStatus"
$summary += "- Overall: $(if ($overallPass) { 'PASS' } else { 'FAIL' })"
$summary += ""
$summary += "Baseline Diff"
$summary += "- Improved: $improved"
$summary += "- Regressed: $regressed"
$summary += "- Changed: $changed"
$summary += "- New: $newCount"
$summary += "- Unchanged: $unchanged"
$summary += ""
$summary += "Per-sample Diff"
$summary += $diffLines

if ($failReasons.Count -gt 0) {
    $summary += ""
    $summary += "Failure Reasons"
    foreach ($reason in $failReasons) {
        $summary += "- $reason"
    }
}

$summaryDir = Split-Path -Parent $SummaryPath
if (-not (Test-Path $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir | Out-Null
}
$summary | Set-Content -Path $SummaryPath
$summary | ForEach-Object { Write-Host $_ }

if ($overallPass) {
    exit 0
}
exit 1
