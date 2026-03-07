param(
    [Parameter(Mandatory = $true)][string]$VrmDir,
    [string]$VrmToMiqExe = ".\AnimiqCore\build_plan_impl\Release\vrm_to_miq.exe",
    [string]$AvatarToolExe = ".\AnimiqCore\build_plan_impl\Release\avatar_tool.exe",
    [string]$OutputRoot = ".\build\reports\miq_converted",
    [switch]$Recurse
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-AvatarResult {
    param([string]$AvatarToolPath, [string]$Path)
    if (-not (Test-Path $Path)) {
        return [PSCustomObject]@{
            exists = $false
            load_ok = $false
            runtime_ready = $false
            parser_stage = "missing-file"
            primary_error = "FILE_NOT_FOUND"
            warning_code_count = 0
            critical_warning_count = 0
            render_visible = $false
            exit_code = -1
        }
    }

    $tmpJson = Join-Path $env:TEMP ("avatar_probe_" + [Guid]::NewGuid().ToString("N") + ".json")
    & $AvatarToolPath $Path "--json-out=$tmpJson" *> $null
    $exitCode = $LASTEXITCODE
    if (-not (Test-Path $tmpJson)) {
        return [PSCustomObject]@{
            exists = $true
            load_ok = $false
            runtime_ready = $false
            parser_stage = "failed"
            primary_error = "JSON_MISSING"
            warning_code_count = 0
            critical_warning_count = 0
            render_visible = $false
            exit_code = $exitCode
        }
    }

    $j = Get-Content -Path $tmpJson -Raw | ConvertFrom-Json
    Remove-Item -Path $tmpJson -Force -ErrorAction SilentlyContinue
    $loadOk = [bool]$j.loadSucceeded
    $stage = "$($j.parserStage)"
    $primary = "$($j.primaryError)"
    $runtimeReady = [bool]($loadOk -and $stage -eq "runtime-ready" -and $primary -eq "NONE")
    return [PSCustomObject]@{
        exists = $true
        load_ok = $loadOk
        runtime_ready = $runtimeReady
        parser_stage = $stage
        primary_error = $primary
        warning_code_count = [int]$j.counts.warningCodeCount
        critical_warning_count = [int]$j.counts.criticalWarningCount
        render_visible = [bool]$j.renderVisibleHeuristic
        exit_code = $exitCode
    }
}

$vrmDirAbs = Resolve-AbsolutePath -Path $VrmDir
$vrmToMiqAbs = Resolve-AbsolutePath -Path $VrmToMiqExe
$avatarToolAbs = Resolve-AbsolutePath -Path $AvatarToolExe
$outRootAbs = Resolve-AbsolutePath -Path $OutputRoot

if (-not (Test-Path $vrmDirAbs)) {
    throw "VrmDir not found: $vrmDirAbs"
}
if (-not (Test-Path $vrmToMiqAbs)) {
    throw "VrmToMiqExe not found: $vrmToMiqAbs"
}
if (-not (Test-Path $avatarToolAbs)) {
    throw "AvatarToolExe not found: $avatarToolAbs"
}

$miqOutDir = Join-Path $outRootAbs "miq_files"
if (-not (Test-Path $miqOutDir)) {
    New-Item -ItemType Directory -Path $miqOutDir -Force | Out-Null
}

$vrmFiles = if ($Recurse.IsPresent) {
    Get-ChildItem -Path $vrmDirAbs -File -Filter *.vrm -Recurse | Sort-Object FullName
} else {
    Get-ChildItem -Path $vrmDirAbs -File -Filter *.vrm | Sort-Object FullName
}

if ($vrmFiles.Count -eq 0) {
    throw "No .vrm files found under: $vrmDirAbs"
}

$rows = [System.Collections.Generic.List[object]]::new()
$vrmReady = 0
$miqReady = 0
$miqConvertFail = 0
$miqReadyDrop = 0
$miqReadyGain = 0

$i = 0
foreach ($vrm in $vrmFiles) {
    $i++
    $id = ("sample_{0}" -f $i.ToString("000"))
    $miqPath = Join-Path $miqOutDir ($vrm.BaseName + ".miq")
    $diagPath = Join-Path $outRootAbs ("diag_" + $id + ".json")

    & $vrmToMiqAbs "--diag-json" $diagPath $vrm.FullName $miqPath *> $null
    $convertExit = $LASTEXITCODE
    $convertOk = ($convertExit -eq 0 -and (Test-Path $miqPath))
    if (-not $convertOk) {
        $miqConvertFail++
    }

    $vrmProbe = Get-AvatarResult -AvatarToolPath $avatarToolAbs -Path $vrm.FullName
    $miqProbe = Get-AvatarResult -AvatarToolPath $avatarToolAbs -Path $miqPath

    if ($vrmProbe.runtime_ready) { $vrmReady++ }
    if ($miqProbe.runtime_ready) { $miqReady++ }
    if ($vrmProbe.runtime_ready -and -not $miqProbe.runtime_ready) { $miqReadyDrop++ }
    if (-not $vrmProbe.runtime_ready -and $miqProbe.runtime_ready) { $miqReadyGain++ }

    $rows.Add([PSCustomObject]@{
        id = $id
        vrm_path = $vrm.FullName
        miq_path = $miqPath
        convert_ok = $convertOk
        convert_exit_code = $convertExit
        vrm_runtime_ready = $vrmProbe.runtime_ready
        vrm_primary_error = $vrmProbe.primary_error
        vrm_parser_stage = $vrmProbe.parser_stage
        vrm_critical_warning_count = $vrmProbe.critical_warning_count
        miq_runtime_ready = $miqProbe.runtime_ready
        miq_primary_error = $miqProbe.primary_error
        miq_parser_stage = $miqProbe.parser_stage
        miq_critical_warning_count = $miqProbe.critical_warning_count
    })
}

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    vrm_dir = $vrmDirAbs
    vrm_to_miq_exe = $vrmToMiqAbs
    avatar_tool_exe = $avatarToolAbs
    totals = [PSCustomObject]@{
        vrm_total = $vrmFiles.Count
        miq_convert_fail = $miqConvertFail
        vrm_runtime_ready = $vrmReady
        miq_runtime_ready = $miqReady
        miq_runtime_ready_drop = $miqReadyDrop
        miq_runtime_ready_gain = $miqReadyGain
    }
    rows = $rows
}

$jsonPath = Join-Path $outRootAbs "avatar_vrm_to_miq_benchmark_summary.json"
$txtPath = Join-Path $outRootAbs "avatar_vrm_to_miq_benchmark_summary.txt"
$mdPath = Join-Path $outRootAbs "avatar_vrm_to_miq_conversion_summary.md"
if (-not (Test-Path $outRootAbs)) {
    New-Item -ItemType Directory -Path $outRootAbs -Force | Out-Null
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $jsonPath -Encoding UTF8

$lines = @()
$lines += "VRM -> MIQ Benchmark Summary"
$lines += "GeneratedUTC: $($summary.generated_utc)"
$lines += "VrmDir: $($summary.vrm_dir)"
$lines += "Totals: vrm=$($summary.totals.vrm_total), convertFail=$($summary.totals.miq_convert_fail), vrmReady=$($summary.totals.vrm_runtime_ready), miqReady=$($summary.totals.miq_runtime_ready), drop=$($summary.totals.miq_runtime_ready_drop), gain=$($summary.totals.miq_runtime_ready_gain)"
$lines += ""
$lines += "Rows"
foreach ($r in $rows) {
    $lines += "- $($r.id) convert=$($r.convert_ok) vrmReady=$($r.vrm_runtime_ready) miqReady=$($r.miq_runtime_ready) vrmErr=$($r.vrm_primary_error) miqErr=$($r.miq_primary_error)"
}
$lines | Set-Content -Path $txtPath -Encoding UTF8

$md = @()
$md += "# VRM to MIQ Conversion Benchmark"
$md += ""
$md += "- Generated UTC: $($summary.generated_utc)"
$md += "- VRM dir: `"$($summary.vrm_dir)`""
$md += ""
$md += "## Totals"
$md += ""
$md += "| VRM Total | Convert Fail | VRM RuntimeReady | MIQ RuntimeReady | Ready Drop | Ready Gain |"
$md += "|---:|---:|---:|---:|---:|---:|"
$md += "| $($summary.totals.vrm_total) | $($summary.totals.miq_convert_fail) | $($summary.totals.vrm_runtime_ready) | $($summary.totals.miq_runtime_ready) | $($summary.totals.miq_runtime_ready_drop) | $($summary.totals.miq_runtime_ready_gain) |"
$md += ""
$md += "## Rows"
$md += ""
$md += "| Id | Convert | VRM Ready | MIQ Ready | VRM Error | MIQ Error |"
$md += "|---|---|---|---|---|---|"
foreach ($r in $rows) {
    $md += "| $($r.id) | $($r.convert_ok) | $($r.vrm_runtime_ready) | $($r.miq_runtime_ready) | $($r.vrm_primary_error) | $($r.miq_primary_error) |"
}
$md | Set-Content -Path $mdPath -Encoding UTF8

Write-Host "summary_json=$jsonPath"
Write-Host "summary_txt=$txtPath"
Write-Host "summary_md=$mdPath"
