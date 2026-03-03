param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$SidecarPath = ".\\build\\Release\\vsfavatar_sidecar.exe",
    [string]$OutputPath = ".\\build\\reports\\vsfavatar_probe.txt",
    [int]$MaxFiles = 20,
    [switch]$UseFixedSet,
    [string]$HostTrackStatus = "BLOCKED_XAML_COMPILER",
    [string[]]$FixedSamples = @(
        "NewOnYou.vsfavatar",
        "Character vywjd.vsfavatar",
        "PPU (2).vsfavatar",
        "VRM dkdlrh.vsfavatar"
    )
)

$ErrorActionPreference = "Stop"
$runStart = Get-Date

if (-not (Test-Path $AvatarToolPath)) {
    throw "avatar_tool not found at $AvatarToolPath"
}

if (-not (Test-Path $SidecarPath)) {
    throw "vsfavatar_sidecar not found at $SidecarPath"
}

if (-not (Test-Path $SampleDir)) {
    throw "sample directory not found at $SampleDir"
}

$outDir = Split-Path -Parent $OutputPath
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}

$files = @()
if ($UseFixedSet) {
    $missingFixed = @()
    foreach ($name in $FixedSamples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $files += Get-Item $p
        } else {
            $missingFixed += $name
        }
    }
    if ($missingFixed.Count -gt 0) {
        throw "fixed set missing samples: $($missingFixed -join ', ')"
    }
}

if ($files.Count -eq 0) {
    $files = Get-ChildItem -Path $SampleDir -Filter *.vsfavatar | Select-Object -First $MaxFiles
}
if ($files.Count -eq 0) {
    throw "no .vsfavatar files found under $SampleDir"
}

"VSFAvatar probe report" | Set-Content -Path $OutputPath
"GateInputVersion: 1" | Add-Content -Path $OutputPath
"Generated: $(Get-Date -Format s)" | Add-Content -Path $OutputPath
"SampleDir: $(Resolve-Path $SampleDir)" | Add-Content -Path $OutputPath
"UseFixedSet: $UseFixedSet" | Add-Content -Path $OutputPath
"FileCount: $($files.Count)" | Add-Content -Path $OutputPath
"HostTrackStatus: $HostTrackStatus" | Add-Content -Path $OutputPath
"" | Add-Content -Path $OutputPath

$gateRows = @()
foreach ($f in $files) {
    "---- $($f.Name)" | Add-Content -Path $OutputPath
    & $AvatarToolPath $f.FullName | Add-Content -Path $OutputPath
    $sidecarRaw = & $SidecarPath $f.FullName
    try {
        $sidecar = $sidecarRaw | ConvertFrom-Json
        if ($sidecar.status -ne "ok") {
            throw "sidecar returned non-ok status for $($f.Name): $($sidecar.error_code) $($sidecar.error_message)"
        }
        $sidecarObjectTableParsed = $false
        if ($null -ne $sidecar.object_table_parsed) {
            $sidecarObjectTableParsed = [bool]$sidecar.object_table_parsed
        }
        "  SidecarProbeStage: $($sidecar.probe_stage)" | Add-Content -Path $OutputPath
        "  SidecarPrimaryError: $($sidecar.primary_error_code)" | Add-Content -Path $OutputPath
        "  SidecarObjectTableParsed: $sidecarObjectTableParsed" | Add-Content -Path $OutputPath
        "  SidecarBlockLayout: $($sidecar.selected_block_layout)" | Add-Content -Path $OutputPath
        "  SidecarOffsetFamily: $($sidecar.selected_offset_family)" | Add-Content -Path $OutputPath
        "  SidecarBlock0Hypothesis: $($sidecar.selected_block0_hypothesis)" | Add-Content -Path $OutputPath
        "  SidecarBlock0Attempts: $($sidecar.block0_attempt_count)" | Add-Content -Path $OutputPath
        "  SidecarBlock0Offset: $($sidecar.block0_selected_offset)" | Add-Content -Path $OutputPath
        "  SidecarBlock0ModeSource: $($sidecar.block0_selected_mode_source)" | Add-Content -Path $OutputPath
        "  SidecarReconCandidateCount: $($sidecar.reconstruction_candidate_count)" | Add-Content -Path $OutputPath
        "  SidecarBestCandidateScore: $($sidecar.best_candidate_score)" | Add-Content -Path $OutputPath
        "  SidecarSerializedAttempts: $($sidecar.serialized_attempt_count)" | Add-Content -Path $OutputPath
        "  SidecarSerializedBestPath: $($sidecar.serialized_best_candidate_path)" | Add-Content -Path $OutputPath
        "  SidecarSerializedBestScore: $($sidecar.serialized_best_candidate_score)" | Add-Content -Path $OutputPath
        "  SidecarFailedReadOffset: $($sidecar.failed_block_read_offset)" | Add-Content -Path $OutputPath
        "  SidecarFailedCompressedSize: $($sidecar.failed_block_compressed_size)" | Add-Content -Path $OutputPath
        "  SidecarFailedUncompressedSize: $($sidecar.failed_block_uncompressed_size)" | Add-Content -Path $OutputPath
        $gateRows += [PSCustomObject]@{
            Name = $f.Name
            ParseOk = $true
            Stage = "$($sidecar.probe_stage)"
            Primary = "$($sidecar.primary_error_code)"
            ObjectTableParsed = $sidecarObjectTableParsed
            ReadOffset = [uint64]($sidecar.failed_block_read_offset)
            CSize = [uint64]($sidecar.failed_block_compressed_size)
            USize = [uint64]($sidecar.failed_block_uncompressed_size)
        }
    } catch {
        "  SidecarParseError: failed to parse JSON output" | Add-Content -Path $OutputPath
        $gateRows += [PSCustomObject]@{
            Name = $f.Name
            ParseOk = $false
            Stage = ""
            Primary = ""
            ObjectTableParsed = $false
            ReadOffset = 0
            CSize = 0
            USize = 0
        }
    }
    "" | Add-Content -Path $OutputPath
}

$gateRowsMatch = ($gateRows.Count -eq $files.Count)
if (-not $gateRowsMatch) {
    throw "gate row mismatch: files=$($files.Count), rows=$($gateRows.Count)"
}

$gateA = $true
foreach ($r in $gateRows) {
    if (-not $r.ParseOk -or [string]::IsNullOrWhiteSpace($r.Stage) -or [string]::IsNullOrWhiteSpace($r.Primary)) {
        $gateA = $false
        break
    }
}

$gateB = $false
foreach ($r in $gateRows) {
    if ($r.Stage -eq "failed-serialized" -or $r.Stage -eq "complete") {
        $gateB = $true
        break
    }
}

$gateC = $true
foreach ($r in $gateRows) {
    if ($r.Primary -eq "DATA_BLOCK_READ_FAILED") {
        if ($r.ReadOffset -le 0 -or $r.CSize -le 0 -or $r.USize -le 0) {
            $gateC = $false
            break
        }
    }
}

$gateD = $false
foreach ($r in $gateRows) {
    if ($r.Stage -eq "complete" -and $r.ObjectTableParsed -eq $true) {
        if ([string]::IsNullOrWhiteSpace($r.Primary) -or $r.Primary -eq "NONE") {
            $gateD = $true
            break
        }
    }
}

"== Gate Summary ==" | Add-Content -Path $OutputPath
"GateA_NoCrashAndDiagPresent: $(if($gateA){'PASS'}else{'FAIL'})" | Add-Content -Path $OutputPath
"GateB_AtLeastOneFailedSerializedOrComplete: $(if($gateB){'PASS'}else{'FAIL'})" | Add-Content -Path $OutputPath
"GateC_ReadFailureHasOffsetModeSizeEvidence: $(if($gateC){'PASS'}else{'FAIL'})" | Add-Content -Path $OutputPath
"GateD_AtLeastOneCompleteWithObjectTable: $(if($gateD){'PASS'}else{'FAIL'})" | Add-Content -Path $OutputPath
"GateRows: $($gateRows.Count)" | Add-Content -Path $OutputPath
"ParserTrack_DoD: $(if($gateA -and $gateB -and $gateC -and $gateD){'PASS'}else{'FAIL'})" | Add-Content -Path $OutputPath
"HostTrack_DoD: $(if($HostTrackStatus -eq 'READY'){ 'PASS' } else { 'PENDING' })" | Add-Content -Path $OutputPath
$elapsedSec = [Math]::Round(((Get-Date) - $runStart).TotalSeconds, 3)
"RunDurationSec: $elapsedSec" | Add-Content -Path $OutputPath

Write-Host "Report written: $OutputPath"
