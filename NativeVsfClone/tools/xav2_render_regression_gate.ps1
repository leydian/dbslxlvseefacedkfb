param(
    [string]$SampleDir = ".",
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$SummaryPath = ".\build\reports\xav2_render_regression_gate_summary.txt",
    [string]$TargetSamplePattern = "*11-3.xav2",
    [switch]$FailOnRenderWarnings,
    [string]$UnitySnapshotDir = "",
    [string]$NativeSnapshotDir = "",
    [double]$MaxSnapshotMeanAbsDiff = 8.0,
    [switch]$FailOnSnapshotMismatch
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $AvatarToolPath)) {
    throw "avatar_tool not found: $AvatarToolPath"
}
if (-not (Test-Path $SampleDir)) {
    throw "sample dir not found: $SampleDir"
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
            $r.WarningCodes -eq "XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED" -or
            $r.WarningCodes -eq "XAV3_SKELETON_PAYLOAD_MISSING" -or
            $r.WarningCodes -eq "XAV3_SKELETON_MESH_BIND_MISMATCH" -or
            $r.WarningCodes -eq "XAV3_SKINNING_MATRIX_INVALID") {
            $gate4 = $false
            break
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
if (-not $FailOnSnapshotMismatch) {
    $gate5 = $true
}

$overall = $gate1 -and $gate2 -and $gate3 -and $gate4 -and $gate5

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
$summary += "- GateX5 (snapshot diff threshold): $(if($gate5){'PASS'}else{'FAIL'})"
$summary += "- Overall: $(if($overall){'PASS'}else{'FAIL'})"
$summary += "- SnapshotThreshold: $MaxSnapshotMeanAbsDiff"
$summary += ""
$summary += "Rows"
foreach ($r in $rows) {
    $summary += "- $($r.Name): compat=$($r.Compat), stage=$($r.ParserStage), error=$($r.PrimaryError), last_warning_code=$($r.WarningCodes)"
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

if (-not $overall) {
    exit 1
}
