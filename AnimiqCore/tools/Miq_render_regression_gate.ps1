param(
    [string]$SampleDir = ".",
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$SummaryPath = ".\build\reports\miq_render_regression_gate_summary.txt",
    [string]$JsonSummaryPath = ".\build\reports\miq_render_regression_gate_summary.json",
    [string]$SampleManifestPath = "",
    [string]$TargetSamplePattern = "*11-3.miq",
    [int]$MinSampleCount = 10,
    [switch]$FailOnRenderWarnings,
    [switch]$FailOnAnyWarnings,
    [switch]$FailOnManifestMismatch,
    [string]$UnitySnapshotDir = "",
    [string]$NativeSnapshotDir = "",
    [double]$MaxSnapshotMeanAbsDiff = 8.0,
    [switch]$FailOnSnapshotMismatch,
    [switch]$RequireSnapshotParity
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $AvatarToolPath)) {
    throw "avatar_tool not found: $AvatarToolPath"
}
if (-not (Test-Path $SampleDir)) {
    throw "sample dir not found: $SampleDir"
}
$manifestByName = @{}
$manifestRequiredNames = @()
if (-not [string]::IsNullOrWhiteSpace($SampleManifestPath)) {
    if (-not (Test-Path $SampleManifestPath)) {
        throw "sample manifest not found: $SampleManifestPath"
    }
    $manifestRaw = Get-Content -Path $SampleManifestPath -Raw
    $manifest = $manifestRaw | ConvertFrom-Json
    if ($null -eq $manifest -or $null -eq $manifest.samples) {
        throw "sample manifest schema invalid: expected root.samples[]"
    }
    foreach ($entry in $manifest.samples) {
        if ($null -eq $entry.name -or [string]::IsNullOrWhiteSpace([string]$entry.name)) {
            throw "sample manifest schema invalid: each sample requires name"
        }
        $key = ([string]$entry.name).Trim().ToLowerInvariant()
        $manifestByName[$key] = $entry
        $manifestRequiredNames += $key
    }
}

function Normalize-Key([string]$v) {
    if ([string]::IsNullOrWhiteSpace($v)) { return "" }
    return $v.Trim().ToLowerInvariant()
}

function Get-ImageMeanAbsDiff([string]$refPath, [string]$testPath) {
    Add-Type -AssemblyName System.Drawing
    $refBmp = $null
    $testBmp = $null
    try {
        $refBmp = [System.Drawing.Bitmap]::new($refPath)
        $testBmp = [System.Drawing.Bitmap]::new($testPath)
        if ($refBmp.Width -ne $testBmp.Width -or $refBmp.Height -ne $testBmp.Height) {
            return [double]::PositiveInfinity
        }
        $sum = 0.0
        $count = [double]($refBmp.Width * $refBmp.Height * 3)
        for ($y = 0; $y -lt $refBmp.Height; $y++) {
            for ($x = 0; $x -lt $refBmp.Width; $x++) {
                $a = $refBmp.GetPixel($x, $y)
                $b = $testBmp.GetPixel($x, $y)
                $sum += [math]::Abs([double]$a.R - [double]$b.R)
                $sum += [math]::Abs([double]$a.G - [double]$b.G)
                $sum += [math]::Abs([double]$a.B - [double]$b.B)
            }
        }
        return ($sum / $count)
    }
    finally {
        if ($null -ne $refBmp) { $refBmp.Dispose() }
        if ($null -ne $testBmp) { $testBmp.Dispose() }
    }
}

$files = Get-ChildItem -Path $SampleDir -Filter *.miq
if ($files.Count -eq 0) {
    throw "no .miq files found under: $SampleDir"
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
        WarningCodes = @()
        WarningMeta = @()
        LastWarningCode = ""
        LastWarningSeverity = ""
        LastWarningCategory = ""
        LastWarningCritical = $false
        CriticalWarningCount = 0
        WarningInfoCount = 0
        WarningWarnCount = 0
        WarningErrorCount = 0
        FailureReason = ""
        SampleClass = ""
        ExpectedPrimaryError = ""
        ExpectedMaxWarningCodes = -1
        ManifestExpected = $false
    }
    $nameKey = $f.Name.Trim().ToLowerInvariant()
    if ($manifestByName.ContainsKey($nameKey)) {
        $entry = $manifestByName[$nameKey]
        $row.ManifestExpected = $true
        $row.SampleClass = if ($null -ne $entry.sample_class) { [string]$entry.sample_class } else { "" }
        $row.ExpectedPrimaryError = if ($null -ne $entry.expected_primary_error) { [string]$entry.expected_primary_error } else { "" }
        $row.ExpectedMaxWarningCodes = if ($null -ne $entry.expected_max_warning_codes) { [int]$entry.expected_max_warning_codes } else { -1 }
    }
    foreach ($line in $out) {
        if ($line -match '^\s*Compat:\s*(.+)$') { $row.Compat = $matches[1].Trim() }
        elseif ($line -match '^\s*ParserStage:\s*(.+)$') { $row.ParserStage = $matches[1].Trim() }
        elseif ($line -match '^\s*PrimaryError:\s*(.+)$') { $row.PrimaryError = $matches[1].Trim() }
        elseif ($line -match '^\s*WarningCodes:\s*(\d+)$') { $row.WarningCount = [int]$matches[1] }
        elseif ($line -match '^\s*WarningCode\[\d+\]:\s*(.+)$') { $row.WarningCodes += $matches[1].Trim() }
        elseif ($line -match '^\s*WarningCodeMeta\[\d+\]:\s*severity=([^,]+),\s*category=([^,]+),\s*critical=(true|false)\s*$') {
            $row.WarningMeta += [PSCustomObject]@{
                Severity = $matches[1].Trim().ToLowerInvariant()
                Category = $matches[2].Trim().ToLowerInvariant()
                Critical = ($matches[3].Trim().ToLowerInvariant() -eq "true")
            }
        }
        elseif ($line -match '^\s*LastWarningCode:\s*(.+)$') { $row.LastWarningCode = $matches[1].Trim() }
        elseif ($line -match '^\s*LastWarningSeverity:\s*(.+)$') { $row.LastWarningSeverity = $matches[1].Trim().ToLowerInvariant() }
        elseif ($line -match '^\s*LastWarningCategory:\s*(.+)$') { $row.LastWarningCategory = $matches[1].Trim().ToLowerInvariant() }
        elseif ($line -match '^\s*LastWarningCritical:\s*(true|false)\s*$') { $row.LastWarningCritical = ($matches[1].Trim().ToLowerInvariant() -eq "true") }
        elseif ($line -match '^\s*CriticalWarningCount:\s*(\d+)$') { $row.CriticalWarningCount = [int]$matches[1] }
        elseif ($line -match '^\s*WarningInfoCount:\s*(\d+)$') { $row.WarningInfoCount = [int]$matches[1] }
        elseif ($line -match '^\s*WarningWarnCount:\s*(\d+)$') { $row.WarningWarnCount = [int]$matches[1] }
        elseif ($line -match '^\s*WarningErrorCount:\s*(\d+)$') { $row.WarningErrorCount = [int]$matches[1] }
    }
    if ($row.WarningMeta.Count -eq 0 -and $row.WarningCodes.Count -gt 0) {
        foreach ($code in $row.WarningCodes) {
            $isCritical = $code -in @(
                "MIQ_SKINNING_STATIC_DISABLED",
                "MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED",
                "XAV3_SKELETON_PAYLOAD_MISSING",
                "XAV3_SKELETON_MESH_BIND_MISMATCH",
                "XAV3_SKINNING_MATRIX_INVALID",
                "MIQ_UNKNOWN_SECTION_NOT_ALLOWED"
            )
            if (-not $isCritical -and $code.StartsWith("MIQ_SHADER_PARITY_")) {
                $isCritical = $true
            }
            $row.WarningMeta += [PSCustomObject]@{
                Severity = "warn"
                Category = "render"
                Critical = $isCritical
            }
            if ($isCritical) {
                $row.CriticalWarningCount++
            }
        }
    }
    $rows += [PSCustomObject]$row
}

$gate0 = $rows.Count -ge $MinSampleCount
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
        if ($r.CriticalWarningCount -gt 0) {
            $gate4 = $false
            if ([string]::IsNullOrWhiteSpace($r.FailureReason)) {
                $r.FailureReason = "critical-warning-present"
            }
            break
        }
    }
}
$gate6 = $true
if ($FailOnAnyWarnings) {
    foreach ($r in $rows) {
        if ($r.WarningCodes.Count -gt 0) {
            $gate6 = $false
            if ([string]::IsNullOrWhiteSpace($r.FailureReason)) {
                $r.FailureReason = "warning-codes-present"
            }
            break
        }
    }
}
$gate7 = $true
if ($FailOnManifestMismatch -and $manifestRequiredNames.Count -gt 0) {
    $presentSet = @{}
    foreach ($r in $rows) {
        $presentSet[$r.Name.Trim().ToLowerInvariant()] = $true
    }
    foreach ($requiredName in $manifestRequiredNames) {
        if (-not $presentSet.ContainsKey($requiredName)) {
            $gate7 = $false
            break
        }
    }
    if ($gate7) {
        foreach ($r in $rows) {
            if (-not $r.ManifestExpected) {
                continue
            }
            if (-not [string]::IsNullOrWhiteSpace($r.ExpectedPrimaryError)) {
                if (-not ($r.PrimaryError.ToUpperInvariant() -eq $r.ExpectedPrimaryError.ToUpperInvariant())) {
                    $gate7 = $false
                    if ([string]::IsNullOrWhiteSpace($r.FailureReason)) {
                        $r.FailureReason = "manifest-primary-error-mismatch"
                    }
                    break
                }
            }
            if ($r.ExpectedMaxWarningCodes -ge 0 -and $r.WarningCodes.Count -gt $r.ExpectedMaxWarningCodes) {
                $gate7 = $false
                if ([string]::IsNullOrWhiteSpace($r.FailureReason)) {
                    $r.FailureReason = "manifest-warning-threshold-exceeded"
                }
                break
            }
        }
    }
}

$gate5 = $true
$snapshotRows = @()
$hasSnapshotInputs = (-not [string]::IsNullOrWhiteSpace($UnitySnapshotDir)) -or (-not [string]::IsNullOrWhiteSpace($NativeSnapshotDir))
if ($hasSnapshotInputs) {
    if ([string]::IsNullOrWhiteSpace($UnitySnapshotDir) -or [string]::IsNullOrWhiteSpace($NativeSnapshotDir)) {
        $gate5 = $false
    } elseif (-not (Test-Path $UnitySnapshotDir) -or -not (Test-Path $NativeSnapshotDir)) {
        $gate5 = $false
    } else {
        $unityPngs = Get-ChildItem -Path $UnitySnapshotDir -Filter *.png
        $nativePngs = Get-ChildItem -Path $NativeSnapshotDir -Filter *.png
        $nativeByName = @{}
        foreach ($n in $nativePngs) {
            $nativeByName[(Normalize-Key $n.Name)] = $n.FullName
        }
        foreach ($u in $unityPngs) {
            $key = Normalize-Key $u.Name
            if (-not $nativeByName.ContainsKey($key)) {
                $snapshotRows += [PSCustomObject]@{
                    Name = $u.Name
                    MeanAbsDiff = [double]::PositiveInfinity
                    Status = "MISSING_NATIVE"
                }
                $gate5 = $false
                continue
            }
            $diff = Get-ImageMeanAbsDiff -refPath $u.FullName -testPath $nativeByName[$key]
            $ok = [double]::IsInfinity($diff) -eq $false -and $diff -le $MaxSnapshotMeanAbsDiff
            $snapshotRows += [PSCustomObject]@{
                Name = $u.Name
                MeanAbsDiff = $diff
                Status = $(if ($ok) { "PASS" } else { "FAIL" })
            }
            if (-not $ok) {
                $gate5 = $false
            }
        }
    }
}
if ($RequireSnapshotParity -and -not $hasSnapshotInputs) {
    $gate5 = $false
}
if ((-not $FailOnSnapshotMismatch) -and (-not $RequireSnapshotParity)) {
    $gate5 = $true
}

foreach ($r in $rows) {
    if ([string]::IsNullOrWhiteSpace($r.FailureReason) -and -not "$($r.ParserStage)".ToLowerInvariant().Contains("runtime-ready")) {
        $r.FailureReason = "parser-stage-not-runtime-ready"
    }
    if ([string]::IsNullOrWhiteSpace($r.FailureReason) -and -not "$($r.PrimaryError)".ToUpperInvariant().Contains("NONE")) {
        $r.FailureReason = "primary-error-not-none"
    }
    if ([string]::IsNullOrWhiteSpace($r.FailureReason)) {
        $r.FailureReason = "none"
    }
}

$overall = $gate0 -and $gate1 -and $gate2 -and $gate3 -and $gate4 -and $gate5 -and $gate6 -and $gate7

$summary = @()
$summary += "MIQ Render Regression Gate Summary"
$summary += "Generated: $(Get-Date -Format s)"
$summary += "SampleDir: $(Resolve-Path $SampleDir)"
$summary += "SampleCount: $($rows.Count)"
$summary += ""
$summary += "Gate Results"
$summary += "- GateX0 (minimum sample count >= $MinSampleCount): $(if($gate0){'PASS'}else{'FAIL'})"
$summary += "- GateX1 (all parser stage runtime-ready): $(if($gate1){'PASS'}else{'FAIL'})"
$summary += "- GateX2 (all primary error NONE): $(if($gate2){'PASS'}else{'FAIL'})"
$summary += "- GateX3 (target sample present): $(if($gate3){'PASS'}else{'FAIL'})"
$summary += "- GateX4 (no critical render warnings when strict): $(if($gate4){'PASS'}else{'FAIL'})"
$summary += "- GateX5 (snapshot diff threshold): $(if($gate5){'PASS'}else{'FAIL'})"
$summary += "- GateX6 (no warning codes when strict): $(if($gate6){'PASS'}else{'FAIL'})"
$summary += "- GateX7 (manifest expectation match when strict): $(if($gate7){'PASS'}else{'FAIL'})"
$summary += "- Overall: $(if($overall){'PASS'}else{'FAIL'})"
$summary += "- SnapshotThreshold: $MaxSnapshotMeanAbsDiff"
$summary += ""
$summary += "Rows"
foreach ($r in $rows) {
    $codes = if ($r.WarningCodes.Count -gt 0) { ($r.WarningCodes -join ",") } else { "none" }
    $summary += "- $($r.Name): class=$($r.SampleClass), compat=$($r.Compat), stage=$($r.ParserStage), error=$($r.PrimaryError), warning_codes=$codes, severity_counts=info:$($r.WarningInfoCount)|warn:$($r.WarningWarnCount)|error:$($r.WarningErrorCount), critical=$($r.CriticalWarningCount), last_warning_code=$($r.LastWarningCode), last_warning_severity=$($r.LastWarningSeverity), last_warning_category=$($r.LastWarningCategory), expected_primary_error=$($r.ExpectedPrimaryError), expected_max_warning_codes=$($r.ExpectedMaxWarningCodes), failure_reason=$($r.FailureReason)"
}
if ($snapshotRows.Count -gt 0) {
    $summary += ""
    $summary += "SnapshotRows"
    foreach ($s in $snapshotRows) {
        $summary += "- $($s.Name): mean_abs_diff=$([string]::Format('{0:0.###}', $s.MeanAbsDiff)), status=$($s.Status)"
    }
}

$summaryDir = Split-Path -Parent $SummaryPath
if (-not (Test-Path $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir | Out-Null
}
$summary | Set-Content -Path $SummaryPath -Encoding UTF8
Write-Host "Summary written: $SummaryPath"

$jsonRows = @()
foreach ($r in $rows) {
    $jsonRows += [ordered]@{
        name = $r.Name
        compat = $r.Compat
        parser_stage = $r.ParserStage
        primary_error = $r.PrimaryError
        warning_codes = @($r.WarningCodes)
        warning_info_count = $r.WarningInfoCount
        warning_warn_count = $r.WarningWarnCount
        warning_error_count = $r.WarningErrorCount
        critical_warning_count = $r.CriticalWarningCount
        sample_class = $r.SampleClass
        last_warning_code = $r.LastWarningCode
        last_warning_severity = $r.LastWarningSeverity
        last_warning_category = $r.LastWarningCategory
        expected_primary_error = $r.ExpectedPrimaryError
        expected_max_warning_codes = $r.ExpectedMaxWarningCodes
        failure_reason = $r.FailureReason
    }
}
$jsonSummary = [ordered]@{
    generated = (Get-Date -Format s)
    sample_dir = (Resolve-Path $SampleDir).Path
    sample_count = $rows.Count
    gates = [ordered]@{
        gate_x0_min_sample_count = $gate0
        gate_x1_runtime_ready = $gate1
        gate_x2_primary_none = $gate2
        gate_x3_target_present = $gate3
        gate_x4_no_critical_warnings = $gate4
        gate_x5_snapshot_diff = $gate5
        gate_x6_no_warning_codes = $gate6
        gate_x7_manifest_match = $gate7
        overall = $overall
    }
    rows = $jsonRows
}
$jsonDir = Split-Path -Parent $JsonSummaryPath
if (-not (Test-Path $jsonDir)) {
    New-Item -ItemType Directory -Path $jsonDir | Out-Null
}
($jsonSummary | ConvertTo-Json -Depth 6) | Set-Content -Path $JsonSummaryPath -Encoding UTF8
Write-Host "JSON summary written: $JsonSummaryPath"

if (-not $overall) {
    exit 1
}
