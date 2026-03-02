param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$ReportScriptPath = ".\\tools\\vxavatar_sample_report.ps1",
    [string]$ReportPath = ".\\build\\reports\\vxavatar_probe_latest.txt",
    [string]$SummaryPath = ".\\build\\reports\\vxavatar_gate_summary.txt",
    [string]$SummaryJsonPath = ".\\build\\reports\\vxavatar_gate_summary.json",
    [ValidateSet("quick", "full")][string]$Profile = "quick",
    [switch]$UseFixedSet,
    [switch]$RequireRealFullSamples,
    [string[]]$FixedVxSamples = @(
        "demo_mvp.vxavatar"
    ),
    [string[]]$FixedVxa2Samples = @(
        "demo_mvp.vxa2"
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

function Require-Field {
    param(
        [hashtable]$Sample,
        [string]$Field
    )

    return $Sample.ContainsKey($Field) -and -not [string]::IsNullOrWhiteSpace($Sample[$Field])
}

function Is-OneOf {
    param(
        [string]$Value,
        [string[]]$Allowed
    )

    foreach ($a in $Allowed) {
        if ($Value -eq $a) {
            return $true
        }
    }
    return $false
}

function To-Int {
    param([hashtable]$Sample, [string]$Field)
    if (-not $Sample.ContainsKey($Field)) {
        return 0
    }
    [int]$out = 0
    if ([int]::TryParse($Sample[$Field], [ref]$out)) {
        return $out
    }
    return 0
}

if (-not (Test-Path $ReportScriptPath)) {
    throw "report script not found: $ReportScriptPath"
}

& $ReportScriptPath `
    -SampleDir $SampleDir `
    -AvatarToolPath $AvatarToolPath `
    -OutputPath $ReportPath `
    -Profile $Profile `
    -UseFixedSet:$UseFixedSet `
    -FixedVxSamples $FixedVxSamples `
    -FixedVxa2Samples $FixedVxa2Samples

$current = Parse-Report -Path $ReportPath
$sampleNames = $current.Order

$gateA = $true
$gateB = $true
$gateC = $true
$gateD = $true
$gateE = $true
$failReasons = @()

$requiredFields = @("InputKind", "InputTag", "Format", "Compat", "ParserStage", "PrimaryError")

$foundFixedVx = 0
$foundCorruptVx = 0
$foundFixedVxa2 = 0
$foundCorruptVxa2 = 0
$foundRealFull = 0

foreach ($name in $sampleNames) {
    $sample = $current.Samples[$name]

    foreach ($field in $requiredFields) {
        if (-not (Require-Field -Sample $sample -Field $field)) {
            $gateD = $false
            $failReasons += "GateD: $name missing field '$field'"
        }
    }

    if (-not (Require-Field -Sample $sample -Field "InputKind") -or -not (Require-Field -Sample $sample -Field "InputTag")) {
        continue
    }

    $kind = $sample["InputKind"]
    $tag = $sample["InputTag"]
    $format = $sample["Format"]
    $compat = $sample["Compat"]
    $stage = $sample["ParserStage"]
    $primary = $sample["PrimaryError"]

    if ($kind -eq "VXAvatar" -and $tag -eq "fixed-valid") {
        $foundFixedVx++
        if ($format -ne "VXAvatar" -or $compat -ne "full" -or $stage -ne "runtime-ready" -or $primary -ne "NONE") {
            $gateA = $false
            $failReasons += "GateA: $name expected VXAvatar full/runtime-ready/NONE but got format=$format compat=$compat stage=$stage primary=$primary"
        }

        $meshPayload = To-Int -Sample $sample -Field "MeshPayloads"
        $texturePayload = To-Int -Sample $sample -Field "TexturePayloads"
        if ($meshPayload -lt 1 -or $texturePayload -lt 1) {
            $gateA = $false
            $failReasons += "GateA: $name expected mesh/texture payload counts >= 1 but got mesh=$meshPayload texture=$texturePayload"
        }
    }

    if ($kind -eq "VXAvatar" -and $tag -eq "synthetic-corrupt-vxavatar") {
        $foundCorruptVx++
        if (-not (Is-OneOf -Value $compat -Allowed @("failed", "partial"))) {
            $gateB = $false
            $failReasons += "GateB: $name expected compat failed|partial but got $compat"
        }
        if (-not (Is-OneOf -Value $primary -Allowed @("VX_SCHEMA_INVALID", "VX_UNSUPPORTED_COMPRESSION"))) {
            $gateB = $false
            $failReasons += "GateB: $name expected primary VX_SCHEMA_INVALID|VX_UNSUPPORTED_COMPRESSION but got $primary"
        }
    }

    if ($kind -eq "VXA2" -and $tag -eq "fixed-valid") {
        $foundFixedVxa2++
        if ($format -ne "VXA2") {
            $gateC = $false
            $failReasons += "GateC: $name expected format VXA2 but got $format"
        }
        if (-not (Is-OneOf -Value $stage -Allowed @("parse", "resolve", "payload", "runtime-ready"))) {
            $gateC = $false
            $failReasons += "GateC: $name parser stage is invalid: $stage"
        }
        if ([string]::IsNullOrWhiteSpace($primary)) {
            $gateC = $false
            $failReasons += "GateC: $name primary error is empty"
        }
    }

    if ($kind -eq "VXA2" -and $tag -eq "synthetic-corrupt-vxa2") {
        $foundCorruptVxa2++
        if (-not (Is-OneOf -Value $primary -Allowed @("VXA2_SECTION_TRUNCATED", "VXA2_SCHEMA_INVALID"))) {
            $gateC = $false
            $failReasons += "GateC: $name expected primary VXA2_SECTION_TRUNCATED|VXA2_SCHEMA_INVALID but got $primary"
        }
    }

    if ($Profile -eq "full" -and $tag -eq "real-full") {
        $foundRealFull++
        if ([string]::IsNullOrWhiteSpace($format) -or [string]::IsNullOrWhiteSpace($compat)) {
            $gateE = $false
            $failReasons += "GateE: $name real-full row has empty format/compat"
        }
    }
}

if ($foundFixedVx -eq 0) {
    $gateA = $false
    $failReasons += "GateA: no fixed-valid VXAvatar sample found"
}
if ($foundCorruptVx -eq 0) {
    $gateB = $false
    $failReasons += "GateB: no synthetic-corrupt-vxavatar sample found"
}
if ($foundFixedVxa2 -eq 0) {
    $gateC = $false
    $failReasons += "GateC: no fixed-valid VXA2 sample found"
}
if ($foundCorruptVxa2 -eq 0) {
    $gateC = $false
    $failReasons += "GateC: no synthetic-corrupt-vxa2 sample found"
}
if ($Profile -eq "full" -and $RequireRealFullSamples -and $foundRealFull -eq 0) {
    $gateE = $false
    $failReasons += "GateE: no real-full sample rows found in full profile"
}

$overallPass = $gateA -and $gateB -and $gateC -and $gateD -and $gateE
$gateAStatus = if ($gateA) { "PASS" } else { "FAIL" }
$gateBStatus = if ($gateB) { "PASS" } else { "FAIL" }
$gateCStatus = if ($gateC) { "PASS" } else { "FAIL" }
$gateDStatus = if ($gateD) { "PASS" } else { "FAIL" }
$gateEStatus = if ($gateE) { "PASS" } else { "FAIL" }

$summary = @()
$summary += "VXAvatar/VXA2 Quality Gate Summary"
$summary += "Generated: $(Get-Date -Format s)"
$summary += "ReportPath: $ReportPath"
$summary += "Profile: $Profile"
$summary += "UseFixedSet: $UseFixedSet"
$summary += ""
$summary += "Gate Results"
$summary += "- GateA (fixed VXAvatar success contract): $gateAStatus"
$summary += "- GateB (synthetic VXAvatar corruption handling): $gateBStatus"
$summary += "- GateC (VXA2 fixed + corruption checks): $gateCStatus"
$summary += "- GateD (required output fields): $gateDStatus"
$summary += "- GateE (full profile real-sample contract): $gateEStatus"
$summary += "- Overall: $(if ($overallPass) { 'PASS' } else { 'FAIL' })"
$summary += ""
$summary += "Sample Coverage"
$summary += "- FixedVX: $foundFixedVx"
$summary += "- CorruptVX: $foundCorruptVx"
$summary += "- FixedVXA2: $foundFixedVxa2"
$summary += "- CorruptVXA2: $foundCorruptVxa2"
$summary += "- RealFull: $foundRealFull"

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

$jsonSummary = [ordered]@{
    generated = (Get-Date -Format s)
    report_path = $ReportPath
    profile = $Profile
    use_fixed_set = [bool]$UseFixedSet
    gates = [ordered]@{
        gate_a = $gateAStatus
        gate_b = $gateBStatus
        gate_c = $gateCStatus
        gate_d = $gateDStatus
        gate_e = $gateEStatus
        overall = if ($overallPass) { "PASS" } else { "FAIL" }
    }
    coverage = [ordered]@{
        fixed_vx = $foundFixedVx
        corrupt_vx = $foundCorruptVx
        fixed_vxa2 = $foundFixedVxa2
        corrupt_vxa2 = $foundCorruptVxa2
        real_full = $foundRealFull
    }
    failure_reasons = $failReasons
}

$jsonSummary | ConvertTo-Json -Depth 5 | Set-Content -Path $SummaryJsonPath

if ($overallPass) {
    exit 0
}
exit 1
