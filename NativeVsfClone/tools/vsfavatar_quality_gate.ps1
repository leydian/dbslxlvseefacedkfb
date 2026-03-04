param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$SidecarPath = ".\\build\\Release\\vsfavatar_sidecar.exe",
    [string]$ReportScriptPath = ".\\tools\\vsfavatar_sample_report.ps1",
    [string]$ReportPath = ".\\build\\reports\\vsfavatar_probe_latest_after_gate.txt",
    [string]$BaselinePath = ".\\build\\reports\\vsfavatar_probe_fixed.txt",
    [string]$SummaryPath = ".\\build\\reports\\vsfavatar_gate_summary.txt",
    [string]$AggregateCsvPath = ".\\build\\reports\\vsfavatar_gate_aggregate.csv",
    [string]$AggregateSummaryPath = ".\\build\\reports\\vsfavatar_gate_aggregate.txt",
    [string]$HostTrackStatus = "AUTO",
    [string]$HostPublishReportPath = ".\\build\\reports\\host_publish_latest.txt",
    [string]$WinUiDiagnosticManifestPath = ".\\build\\reports\\winui\\winui_diagnostic_manifest.json",
    [switch]$UseSmoke,
    [int]$SmokeMaxFiles = 2,
    [switch]$UseFixedSet,
    [string[]]$FixedSamples = @(
        "NewOnYou.vsfavatar",
        "Character vywjd.vsfavatar",
        "PPU (2).vsfavatar",
        "VRM dkdlrh.vsfavatar"
    )
)

$ErrorActionPreference = "Stop"
$runStart = Get-Date

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
    $expectedCount = $null
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^([^:]+):\s*(.*)$' -and $null -eq $currentName) {
            $key = $matches[1].Trim()
            $value = $matches[2].Trim()
            $result.Header[$key] = $value
            if ($key -eq "FileCount") {
                [int]$fc = 0
                if ([int]::TryParse($value, [ref]$fc)) {
                    $expectedCount = $fc
                }
            }
            continue
        }
        if ($line -match '^----\s+(.+\.vsfavatar)$') {
            if ($null -ne $expectedCount -and $result.Order.Count -ge $expectedCount) {
                continue
            }
            $candidateName = $matches[1].Trim()
            $j = $i + 1
            while ($j -lt $lines.Count -and [string]::IsNullOrWhiteSpace($lines[$j])) {
                $j++
            }
            if ($j -lt $lines.Count -and (($lines[$j] -like 'Load *') -or ($lines[$j] -like '  Sidecar*'))) {
                $currentName = $candidateName
                if (-not $result.Samples.ContainsKey($currentName)) {
                    $result.Samples[$currentName] = @{}
                    $result.Order += $currentName
                }
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

function Resolve-HostTrackStatus {
    param(
        [string]$RequestedStatus,
        [string]$PublishReportPath,
        [string]$DiagnosticManifestPath
    )

    $resolved = [ordered]@{
        Status = "UNKNOWN"
        Reason = "Host status not resolved."
        EvidencePath = ""
    }

    if (-not [string]::IsNullOrWhiteSpace($RequestedStatus) -and $RequestedStatus -ne "AUTO") {
        $resolved.Status = $RequestedStatus
        $resolved.Reason = "Explicit HostTrackStatus override was provided."
        return $resolved
    }

    $hasWpfExe = Test-Path ".\dist\wpf\WpfHost.exe"
    $hasWinUiExe = Test-Path ".\dist\winui\WinUiHost.exe"
    if ($hasWpfExe -and $hasWinUiExe) {
        $resolved.Status = "PASS_WPF_AND_WINUI"
        $resolved.Reason = "WPF/WinUI publish outputs detected."
        $resolved.EvidencePath = ".\dist"
        return $resolved
    }
    if ($hasWpfExe) {
        $resolved.Status = "PASS_WPF_BASELINE"
        $resolved.Reason = "WPF publish output detected (WPF-first host policy)."
        $resolved.EvidencePath = ".\dist\wpf\WpfHost.exe"
        return $resolved
    }

    if (Test-Path $DiagnosticManifestPath) {
        try {
            $manifest = Get-Content -Raw -Path $DiagnosticManifestPath | ConvertFrom-Json
            $failureClass = "$($manifest.failure_class)"
            $resolved.EvidencePath = $DiagnosticManifestPath
            switch ($failureClass) {
                "TOOLCHAIN_MISSING_DOTNET8" { $resolved.Status = "BLOCKED_TOOLCHAIN_MISSING_DOTNET8" }
                "TOOLCHAIN_PRECONDITION_FAILED" { $resolved.Status = "BLOCKED_TOOLCHAIN_PRECONDITION" }
                "TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED" { $resolved.Status = "BLOCKED_XAML_PLATFORM_UNSUPPORTED" }
                "NUGET_SOURCE_UNREACHABLE" { $resolved.Status = "BLOCKED_NUGET_SOURCE" }
                "MANAGED_XAML_TASK_MISSING_DEP" { $resolved.Status = "BLOCKED_MANAGED_XAML_DEPENDENCY" }
                "XAML_COMPILER_EXEC_FAIL" { $resolved.Status = "BLOCKED_XAML_COMPILER" }
                default { $resolved.Status = "FAIL" }
            }
            $resolved.Reason = "Resolved from winui diagnostics manifest failure_class=$failureClass."
            return $resolved
        } catch {
            $resolved.Status = "FAIL"
            $resolved.Reason = "Failed to parse WinUI diagnostics manifest."
            $resolved.EvidencePath = $DiagnosticManifestPath
            return $resolved
        }
    }

    if (Test-Path $PublishReportPath) {
        $reportLines = Get-Content -Path $PublishReportPath
        $resolved.EvidencePath = $PublishReportPath
        $wpfPass = ($reportLines | Select-String -Pattern "WPF exe:" -SimpleMatch -Quiet)
        $winUiPass = ($reportLines | Select-String -Pattern "WinUI exe:" -SimpleMatch -Quiet)
        $winUiFailed = ($reportLines | Select-String -Pattern "WinUI publish: failed" -SimpleMatch -Quiet)

        if ($wpfPass -and $winUiPass) {
            $resolved.Status = "PASS_WPF_AND_WINUI"
            $resolved.Reason = "WPF/WinUI publish entries found in host publish report."
            return $resolved
        }
        if ($wpfPass) {
            $resolved.Status = "PASS_WPF_BASELINE"
            $resolved.Reason = "WPF publish entry found in host publish report (WPF-first host policy)."
            return $resolved
        }
        if ($winUiFailed) {
            $resolved.Status = "BLOCKED_XAML_COMPILER"
            $resolved.Reason = "WinUI publish failure found in host publish report."
            return $resolved
        }

        $resolved.Status = "UNKNOWN"
        $resolved.Reason = "Host publish report found but no definitive WinUI outcome."
        return $resolved
    }

    $resolved.Status = "UNKNOWN"
    $resolved.Reason = "No host publish evidence found."
    return $resolved
}

function Test-IsHostTrackPass {
    param([string]$Status)
    if ([string]::IsNullOrWhiteSpace($Status)) {
        return $false
    }
    return $Status -eq "PASS" -or $Status.StartsWith("PASS_")
}

if (-not (Test-Path $ReportScriptPath)) {
    throw "report script not found: $ReportScriptPath"
}
if ($UseSmoke -and $UseFixedSet) {
    throw "-UseSmoke and -UseFixedSet cannot be used together"
}
if (-not (Test-Path $BaselinePath)) {
    Write-Warning "baseline report not found: $BaselinePath (continuing with empty baseline)"
}

$hostTrack = Resolve-HostTrackStatus `
    -RequestedStatus $HostTrackStatus `
    -PublishReportPath $HostPublishReportPath `
    -DiagnosticManifestPath $WinUiDiagnosticManifestPath
$resolvedHostTrackStatus = $hostTrack.Status
$resolvedHostTrackReason = $hostTrack.Reason
$resolvedHostTrackEvidencePath = $hostTrack.EvidencePath

if ($UseFixedSet) {
    & $ReportScriptPath `
        -SampleDir $SampleDir `
        -AvatarToolPath $AvatarToolPath `
        -SidecarPath $SidecarPath `
        -OutputPath $ReportPath `
        -HostTrackStatus $resolvedHostTrackStatus `
        -HostTrackStatusReason $resolvedHostTrackReason `
        -HostTrackEvidencePath $resolvedHostTrackEvidencePath `
        -UseFixedSet `
        -FixedSamples $FixedSamples
} else {
    $maxFiles = if ($UseSmoke) { $SmokeMaxFiles } else { 20 }
    & $ReportScriptPath `
        -SampleDir $SampleDir `
        -AvatarToolPath $AvatarToolPath `
        -SidecarPath $SidecarPath `
        -OutputPath $ReportPath `
        -MaxFiles $maxFiles `
        -UseFixedSet:$false `
        -HostTrackStatus $resolvedHostTrackStatus `
        -HostTrackStatusReason $resolvedHostTrackReason `
        -HostTrackEvidencePath $resolvedHostTrackEvidencePath
}

$current = Parse-Report -Path $ReportPath
$baseline = @{
    Header = @{}
    Samples = @{}
    Order = @()
}
if (Test-Path $BaselinePath) {
    $baseline = Parse-Report -Path $BaselinePath
}

$requiredFields = @(
    "SidecarProbeStage",
    "SidecarPrimaryError",
    "SidecarObjectTableParsed",
    "SidecarSerializedAttempts",
    "SidecarSerializedBestPath",
    "SidecarSerializedBestScore",
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
$stageCounts = @{}
$primaryCounts = @{}
$objectTableParsedTrue = 0
$objectTableParsedFalse = 0
$serializedAttempts = @()

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
    if ($stageCounts.ContainsKey($stage)) { $stageCounts[$stage] += 1 } else { $stageCounts[$stage] = 1 }
    $primary = $sample["SidecarPrimaryError"]
    if ($primaryCounts.ContainsKey($primary)) { $primaryCounts[$primary] += 1 } else { $primaryCounts[$primary] = 1 }
    $serializedAttempts += (To-UInt64 $sample["SidecarSerializedAttempts"])

    if ($stage -eq "failed-serialized" -or $stage -eq "complete") {
        $gateB = $true
    }
    $objectTableParsed = $sample["SidecarObjectTableParsed"]
    if ($objectTableParsed -eq "True") { $objectTableParsedTrue++ } else { $objectTableParsedFalse++ }
    if ($stage -eq "complete" -and $objectTableParsed -eq "True" -and
        ([string]::IsNullOrWhiteSpace($primary) -or $primary -eq "NONE")) {
        $gateD = $true
    } elseif ($stage -ne "complete" -or $objectTableParsed -ne "True" -or
        (-not [string]::IsNullOrWhiteSpace($primary) -and $primary -ne "NONE")) {
        if (-not $UseSmoke) {
            $failReasons += "GateD: $name unmet (stage=$stage, object_table_parsed=$objectTableParsed, primary=$primary)"
        }
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
if ((-not $gateD) -and (-not $UseSmoke)) {
    $failReasons += "GateD: no sample reached complete with object_table_parsed=true and no primary error"
}

$gateAStatus = if ($gateA) { "PASS" } else { "FAIL" }
$gateBStatus = if ($gateB) { "PASS" } else { "FAIL" }
$gateCStatus = if ($gateC) { "PASS" } else { "FAIL" }
$gateDStatus = if ($gateD) { "PASS" } else { "FAIL" }
$overallPass = if ($UseSmoke) { $gateA -and $gateB -and $gateC } else { $gateA -and $gateB -and $gateC -and $gateD }
$parserTrackDoD = if ($overallPass) { "PASS" } else { "FAIL" }
$hostTrackDoD = if (Test-IsHostTrackPass -Status $resolvedHostTrackStatus) { "PASS" } else { "PENDING" }
$runDurationSec = [Math]::Round(((Get-Date) - $runStart).TotalSeconds, 3)
$attemptAvg = 0.0
$attemptMax = [uint64]0
if ($serializedAttempts.Count -gt 0) {
    $attemptAvg = [Math]::Round((($serializedAttempts | Measure-Object -Sum).Sum / $serializedAttempts.Count), 3)
    $attemptMax = [uint64](($serializedAttempts | Measure-Object -Maximum).Maximum)
}

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
$summary += "UseSmoke: $UseSmoke"
$summary += "SmokeMaxFiles: $SmokeMaxFiles"
$summary += "RunDurationSec: $runDurationSec"
$summary += ""
$summary += "Parser Track"
$summary += "- GateA (stability + required fields): $gateAStatus"
$summary += "- GateB (>=1 sample reaches failed-serialized|complete): $gateBStatus"
$summary += "- GateC (DATA_BLOCK_READ_FAILED tuple evidence): $gateCStatus"
$summary += "- GateD (>=1 sample reaches complete + object_table_parsed=true + no primary error): $gateDStatus"
$summary += "- GateDBlocking: $(if($UseSmoke){'NO (smoke mode)'}else{'YES'})"
$summary += "- ParserTrack_DoD: $parserTrackDoD"
$summary += ""
$summary += "Host Track"
$summary += "- HostTrackStatus: $resolvedHostTrackStatus"
$summary += "- HostTrackStatusReason: $resolvedHostTrackReason"
$summary += "- HostTrackEvidencePath: $resolvedHostTrackEvidencePath"
$summary += "- HostTrack_DoD: $hostTrackDoD"
$summary += ""
$summary += "Gate Overall"
$summary += "- Overall: $(if ($overallPass) { 'PASS' } else { 'FAIL' })"
$summary += ""
$summary += "Probe Metrics"
$summary += "- SerializedAttempts_Avg: $attemptAvg"
$summary += "- SerializedAttempts_Max: $attemptMax"
$summary += "- ObjectTableParsed_True: $objectTableParsedTrue"
$summary += "- ObjectTableParsed_False: $objectTableParsedFalse"
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

$aggregateRows = @()
foreach ($k in ($stageCounts.Keys | Sort-Object)) {
    $aggregateRows += [PSCustomObject]@{
        Metric = "Stage"
        Key = "$k"
        Count = [int]$stageCounts[$k]
    }
}
foreach ($k in ($primaryCounts.Keys | Sort-Object)) {
    $aggregateRows += [PSCustomObject]@{
        Metric = "PrimaryError"
        Key = "$k"
        Count = [int]$primaryCounts[$k]
    }
}
$aggregateRows += [PSCustomObject]@{ Metric = "ObjectTableParsed"; Key = "True"; Count = $objectTableParsedTrue }
$aggregateRows += [PSCustomObject]@{ Metric = "ObjectTableParsed"; Key = "False"; Count = $objectTableParsedFalse }
$aggregateRows += [PSCustomObject]@{ Metric = "SerializedAttempts"; Key = "Avg"; Count = $attemptAvg }
$aggregateRows += [PSCustomObject]@{ Metric = "SerializedAttempts"; Key = "Max"; Count = $attemptMax }

$aggregateSummary = @()
$aggregateSummary += "VSFAvatar Aggregate Metrics"
$aggregateSummary += "Generated: $(Get-Date -Format s)"
$aggregateSummary += "RunDurationSec: $runDurationSec"
$aggregateSummary += ""
$aggregateSummary += "Stage Distribution"
foreach ($k in ($stageCounts.Keys | Sort-Object)) {
    $aggregateSummary += "- ${k}: $($stageCounts[$k])"
}
$aggregateSummary += ""
$aggregateSummary += "Primary Error Distribution"
foreach ($k in ($primaryCounts.Keys | Sort-Object)) {
    $aggregateSummary += "- ${k}: $($primaryCounts[$k])"
}
$aggregateSummary += ""
$aggregateSummary += "Object Table Parsed"
$aggregateSummary += "- True: $objectTableParsedTrue"
$aggregateSummary += "- False: $objectTableParsedFalse"
$aggregateSummary += ""
$aggregateSummary += "Serialized Attempts"
$aggregateSummary += "- Avg: $attemptAvg"
$aggregateSummary += "- Max: $attemptMax"

$summaryDir = Split-Path -Parent $SummaryPath
if (-not (Test-Path $summaryDir)) {
    New-Item -ItemType Directory -Path $summaryDir | Out-Null
}
$summary | Set-Content -Path $SummaryPath
$aggregateDir = Split-Path -Parent $AggregateCsvPath
if (-not (Test-Path $aggregateDir)) {
    New-Item -ItemType Directory -Path $aggregateDir | Out-Null
}
$aggregateRows | Export-Csv -Path $AggregateCsvPath -NoTypeInformation
$aggregateSummaryDir = Split-Path -Parent $AggregateSummaryPath
if (-not (Test-Path $aggregateSummaryDir)) {
    New-Item -ItemType Directory -Path $aggregateSummaryDir | Out-Null
}
$aggregateSummary | Set-Content -Path $AggregateSummaryPath
$summary | ForEach-Object { Write-Host $_ }
Write-Host "Aggregate CSV: $AggregateCsvPath"
Write-Host "Aggregate Summary: $AggregateSummaryPath"

if ($overallPass) {
    exit 0
}
exit 1
