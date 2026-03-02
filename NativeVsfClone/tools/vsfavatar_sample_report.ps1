param(
    [string]$SampleDir = "..\\sample",
    [string]$AvatarToolPath = ".\\build\\Release\\avatar_tool.exe",
    [string]$SidecarPath = ".\\build\\Release\\vsfavatar_sidecar.exe",
    [string]$OutputPath = ".\\build\\reports\\vsfavatar_probe.txt",
    [int]$MaxFiles = 20,
    [switch]$UseFixedSet,
    [string[]]$FixedSamples = @(
        "NewOnYou.vsfavatar",
        "Character vywjd.vsfavatar",
        "PPU (2).vsfavatar",
        "VRM dkdlrh.vsfavatar"
    )
)

$ErrorActionPreference = "Stop"

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
    foreach ($name in $FixedSamples) {
        $p = Join-Path $SampleDir $name
        if (Test-Path $p) {
            $files += Get-Item $p
        }
    }
}

if ($files.Count -eq 0) {
    $files = Get-ChildItem -Path $SampleDir -Filter *.vsfavatar | Select-Object -First $MaxFiles
}
if ($files.Count -eq 0) {
    throw "no .vsfavatar files found under $SampleDir"
}

"VSFAvatar probe report" | Set-Content -Path $OutputPath
"Generated: $(Get-Date -Format s)" | Add-Content -Path $OutputPath
"SampleDir: $(Resolve-Path $SampleDir)" | Add-Content -Path $OutputPath
"UseFixedSet: $UseFixedSet" | Add-Content -Path $OutputPath
"FileCount: $($files.Count)" | Add-Content -Path $OutputPath
"" | Add-Content -Path $OutputPath

foreach ($f in $files) {
    "---- $($f.Name)" | Add-Content -Path $OutputPath
    & $AvatarToolPath $f.FullName | Add-Content -Path $OutputPath
    $sidecarRaw = & $SidecarPath $f.FullName
    try {
        $sidecar = $sidecarRaw | ConvertFrom-Json
        "  SidecarProbeStage: $($sidecar.probe_stage)" | Add-Content -Path $OutputPath
        "  SidecarPrimaryError: $($sidecar.primary_error_code)" | Add-Content -Path $OutputPath
        "  SidecarBlockLayout: $($sidecar.selected_block_layout)" | Add-Content -Path $OutputPath
        "  SidecarOffsetFamily: $($sidecar.selected_offset_family)" | Add-Content -Path $OutputPath
        "  SidecarBlock0Hypothesis: $($sidecar.selected_block0_hypothesis)" | Add-Content -Path $OutputPath
        "  SidecarBlock0Attempts: $($sidecar.block0_attempt_count)" | Add-Content -Path $OutputPath
        "  SidecarBlock0Offset: $($sidecar.block0_selected_offset)" | Add-Content -Path $OutputPath
        "  SidecarBlock0ModeSource: $($sidecar.block0_selected_mode_source)" | Add-Content -Path $OutputPath
        "  SidecarReconCandidateCount: $($sidecar.reconstruction_candidate_count)" | Add-Content -Path $OutputPath
        "  SidecarBestCandidateScore: $($sidecar.best_candidate_score)" | Add-Content -Path $OutputPath
        "  SidecarFailedReadOffset: $($sidecar.failed_block_read_offset)" | Add-Content -Path $OutputPath
        "  SidecarFailedCompressedSize: $($sidecar.failed_block_compressed_size)" | Add-Content -Path $OutputPath
        "  SidecarFailedUncompressedSize: $($sidecar.failed_block_uncompressed_size)" | Add-Content -Path $OutputPath
    } catch {
        "  SidecarParseError: failed to parse JSON output" | Add-Content -Path $OutputPath
    }
    "" | Add-Content -Path $OutputPath
}

Write-Host "Report written: $OutputPath"
