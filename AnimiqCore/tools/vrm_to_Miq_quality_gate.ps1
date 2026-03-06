param(
    [string]$SampleDir = ".\sample",
    [string]$VrmToMiqPath = ".\build\Release\vrm_to_miq.exe",
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$ReportDir = ".\build\reports",
    [string[]]$SampleAllowlist = @(
        "Kikyo_FT Variant.vrm",
        "Kikyo_FT Variant111.vrm",
        "Kikyo_FT Variant112.vrm",
        "MANUKA_FT Variant(Clone).vrm",
        "NewOnYou.vrm"
    ),
    [int]$MaxSamples = 5,
    [double]$TargetProfileReductionPercent = 8.0,
    [double]$TargetSizeReductionPercent = 20.0,
    [double]$TargetWriteTimeReductionPercent = 30.0,
    [double]$TargetBufferReductionPercent = 25.0,
    [switch]$EnforceKpi
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Parse-AvatarToolOutput {
    param([string[]]$Lines)
    $r = [ordered]@{
        Compat = ""
        ParserStage = ""
        PrimaryError = ""
    }
    foreach ($line in $Lines) {
        if ($line -match '^\s*Compat:\s*(.+)$') { $r.Compat = $matches[1].Trim() }
        elseif ($line -match '^\s*ParserStage:\s*(.+)$') { $r.ParserStage = $matches[1].Trim() }
        elseif ($line -match '^\s*PrimaryError:\s*(.+)$') { $r.PrimaryError = $matches[1].Trim() }
    }
    return [PSCustomObject]$r
}

function Pick-Samples {
    param([string]$Dir, [string[]]$Allowlist, [int]$MaxCount)
    $picked = [System.Collections.Generic.List[object]]::new()
    foreach ($name in $Allowlist) {
        $p = Join-Path $Dir $name
        if (Test-Path -LiteralPath $p) {
            $picked.Add((Get-Item -LiteralPath $p))
        }
    }
    if ($picked.Count -eq 0) {
        return @(Get-ChildItem -Path $Dir -Filter *.vrm -File | Sort-Object Name | Select-Object -First $MaxCount)
    } else {
        return @($picked | Select-Object -First $MaxCount)
    }
}

if (-not (Test-Path -LiteralPath $VrmToMiqPath)) { throw "vrm_to_miq not found: $VrmToMiqPath" }
if (-not (Test-Path -LiteralPath $AvatarToolPath)) { throw "avatar_tool not found: $AvatarToolPath" }
if (-not (Test-Path -LiteralPath $SampleDir)) { throw "sample dir not found: $SampleDir" }

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$outDir = Join-Path $ReportDir "vrm_to_miq_gate_outputs"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$allowPath = Join-Path $ReportDir "vrm_to_miq_strict_allowlist.txt"
@(
    "# strict allowlist example"
    "VRM_NODE_TRANSFORM_APPLIED"
    "XAV4_RIG_REQUIRED_HUMANOID_MISSING"
    "MIQ_PHYSICS_REF_MISSING"
    "MIQ_MATERIAL_TYPED_TEXTURE_UNRESOLVED"
) | Set-Content -Path $allowPath -Encoding UTF8

$samples = @(Pick-Samples -Dir $SampleDir -Allowlist $SampleAllowlist -MaxCount $MaxSamples)
if ($samples.Count -eq 0) { throw "no vrm samples found under $SampleDir" }

$rows = [System.Collections.Generic.List[object]]::new()
$gateA = $true # conversion/load pass
$gateB = $true # diagnostics contract
$gateC = $true # strict allowlist smoke
$gateD = $true # kpi
$gateE = $true # profile reduction kpi
$gateQ = $true # quality gate (P0 + runtime validation)

foreach ($sample in $samples) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($sample.Name)
    $optimizedOut = Join-Path $outDir ($base + ".opt.miq")
    $losslessOut = Join-Path $outDir ($base + ".lossless.miq")
    $rawOut = Join-Path $outDir ($base + ".raw.miq")
    $optDiagPath = Join-Path $outDir ($base + ".opt.diag.json")
    $optPerfPath = Join-Path $outDir ($base + ".opt.perf.json")
    $losslessDiagPath = Join-Path $outDir ($base + ".lossless.diag.json")
    $losslessPerfPath = Join-Path $outDir ($base + ".lossless.perf.json")
    $rawDiagPath = Join-Path $outDir ($base + ".raw.diag.json")
    $rawPerfPath = Join-Path $outDir ($base + ".raw.perf.json")

    & $VrmToMiqPath --profile runtime_optimized --diag-json $optDiagPath --perf-metrics-json $optPerfPath $sample.FullName $optimizedOut | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $gateA = $false
        $rows.Add([PSCustomObject]@{ name = $sample.Name; status = "FAIL"; reason = "runtime_optimized convert failed" })
        continue
    }
    & $VrmToMiqPath --profile lossless --diag-json $losslessDiagPath --perf-metrics-json $losslessPerfPath $sample.FullName $losslessOut | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $gateA = $false
        $rows.Add([PSCustomObject]@{ name = $sample.Name; status = "FAIL"; reason = "lossless convert failed" })
        continue
    }
    & $VrmToMiqPath --profile lossless --no-compress --diag-json $rawDiagPath --perf-metrics-json $rawPerfPath $sample.FullName $rawOut | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $gateA = $false
        $rows.Add([PSCustomObject]@{ name = $sample.Name; status = "FAIL"; reason = "raw convert failed" })
        continue
    }

    $probeLines = & $AvatarToolPath $optimizedOut
    $probe = Parse-AvatarToolOutput -Lines $probeLines
    $probeOk = ($probe.Compat -eq "full") -and ($probe.ParserStage -eq "runtime-ready") -and ($probe.PrimaryError -eq "NONE")
    if (-not $probeOk) {
        $gateA = $false
    }

    if (-not (Test-Path -LiteralPath $optDiagPath) -or -not (Test-Path -LiteralPath $optPerfPath) -or
        -not (Test-Path -LiteralPath $losslessDiagPath) -or -not (Test-Path -LiteralPath $losslessPerfPath)) {
        $gateB = $false
    }
    $optDiag = if (Test-Path -LiteralPath $optDiagPath) { Get-Content -Raw -Path $optDiagPath | ConvertFrom-Json } else { $null }
    $optPerf = if (Test-Path -LiteralPath $optPerfPath) { Get-Content -Raw -Path $optPerfPath | ConvertFrom-Json } else { $null }
    $losslessDiag = if (Test-Path -LiteralPath $losslessDiagPath) { Get-Content -Raw -Path $losslessDiagPath | ConvertFrom-Json } else { $null }
    $losslessPerf = if (Test-Path -LiteralPath $losslessPerfPath) { Get-Content -Raw -Path $losslessPerfPath | ConvertFrom-Json } else { $null }
    $rawPerf = if (Test-Path -LiteralPath $rawPerfPath) { Get-Content -Raw -Path $rawPerfPath | ConvertFrom-Json } else { $null }

    if ($null -eq $optDiag -or $null -eq $optPerf -or $null -eq $losslessDiag -or $null -eq $losslessPerf -or $null -eq $rawPerf) {
        $gateB = $false
        $rows.Add([PSCustomObject]@{ name = $sample.Name; status = "FAIL"; reason = "missing diag/perf artifacts" })
        continue
    }
    if (($optDiag.sectionCount -le 0) -or ($optDiag.rawTotalBytes -le 0) -or ($optDiag.writtenTotalBytes -le 0) -or
        ($losslessDiag.sectionCount -le 0) -or ($losslessDiag.rawTotalBytes -le 0) -or ($losslessDiag.writtenTotalBytes -le 0)) {
        $gateB = $false
    }
    if (($optDiag.profile -ne "runtime_optimized") -or ($losslessDiag.profile -ne "lossless")) {
        $gateB = $false
    }
    if (($null -eq $optDiag.runtimeValidation) -or ($null -eq $optDiag.qualityGate)) {
        $gateB = $false
    }

    $qualityOk = $false
    if (($null -ne $optDiag.runtimeValidation) -and ($null -ne $optDiag.qualityGate)) {
        $runtimeReady = [bool]$optDiag.runtimeValidation.runtimeReady
        $qualityPass = [bool]$optDiag.qualityGate.pass
        $qualityOk = $runtimeReady -and $qualityPass
    }
    if (-not $qualityOk) {
        $gateQ = $false
    }

    & $VrmToMiqPath --strict --profile lossless --strict-allowlist $allowPath $sample.FullName (Join-Path $outDir ($base + ".strict.miq")) | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $gateC = $false
    }

    $profileReduction = 0.0
    $sizeReduction = 0.0
    $writeReduction = 0.0
    $bufferReduction = 0.0
    if ([double]$losslessPerf.writtenTotalBytes -gt 0) {
        $profileReduction = (1.0 - ([double]$optPerf.writtenTotalBytes / [double]$losslessPerf.writtenTotalBytes)) * 100.0
    }
    if ([double]$rawPerf.writtenTotalBytes -gt 0) {
        $sizeReduction = (1.0 - ([double]$optPerf.writtenTotalBytes / [double]$rawPerf.writtenTotalBytes)) * 100.0
    }
    if ([double]$rawPerf.timingMs.write -gt 0) {
        $writeReduction = (1.0 - ([double]$optPerf.timingMs.write / [double]$rawPerf.timingMs.write)) * 100.0
    }
    if ([double]$rawPerf.maxPayloadBufferBytes -gt 0) {
        $bufferReduction = (1.0 - ([double]$optPerf.maxPayloadBufferBytes / [double]$rawPerf.maxPayloadBufferBytes)) * 100.0
    }

    $profileKpiOk = ($profileReduction -ge $TargetProfileReductionPercent)
    if (-not $profileKpiOk) {
        $gateE = $false
    }

    $kpiOk = ($sizeReduction -ge $TargetSizeReductionPercent) -and
             ($writeReduction -ge $TargetWriteTimeReductionPercent) -and
             ($bufferReduction -ge $TargetBufferReductionPercent)
    if (-not $kpiOk) {
        $gateD = $false
    }

    $rows.Add([PSCustomObject]@{
        name = $sample.Name
        status = if ($probeOk) { "PASS" } else { "FAIL" }
        compat = $probe.Compat
        parser_stage = $probe.ParserStage
        primary_error = $probe.PrimaryError
        section_count = [int]$optDiag.sectionCount
        compressed_sections = [int]$optDiag.compressedSectionCount
        profile_reduction_pct = [Math]::Round($profileReduction, 2)
        size_reduction_pct = [Math]::Round($sizeReduction, 2)
        write_reduction_pct = [Math]::Round($writeReduction, 2)
        buffer_reduction_pct = [Math]::Round($bufferReduction, 2)
        quality_ok = $qualityOk
        profile_kpi_ok = $profileKpiOk
        kpi_ok = $kpiOk
    })
}

$overall = $gateA -and $gateB -and $gateC -and $gateQ -and (($EnforceKpi -and $gateD -and $gateE) -or (-not $EnforceKpi))
$summary = [ordered]@{
    generated = (Get-Date).ToString("s")
    enforce_kpi = [bool]$EnforceKpi
    kpi_targets = [ordered]@{
        profile_reduction_percent = $TargetProfileReductionPercent
        size_reduction_percent = $TargetSizeReductionPercent
        write_time_reduction_percent = $TargetWriteTimeReductionPercent
        buffer_reduction_percent = $TargetBufferReductionPercent
    }
    gates = [ordered]@{
        gate_a_conversion_and_load = if ($gateA) { "PASS" } else { "FAIL" }
        gate_b_diag_contract = if ($gateB) { "PASS" } else { "FAIL" }
        gate_c_strict_allowlist_smoke = if ($gateC) { "PASS" } else { "FAIL" }
        gate_q_quality = if ($gateQ) { "PASS" } else { "FAIL" }
        gate_e_profile_kpi = if ($gateE) { "PASS" } else { "FAIL" }
        gate_d_kpi = if ($gateD) { "PASS" } else { "FAIL" }
        overall = if ($overall) { "PASS" } else { "FAIL" }
    }
    rows = $rows
}

$summaryJsonPath = Join-Path $ReportDir "vrm_to_miq_quality_gate_summary.json"
$summaryTxtPath = Join-Path $ReportDir "vrm_to_miq_quality_gate_summary.txt"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryJsonPath -Encoding UTF8

$lines = @()
$lines += "VRM_TO_MIQ Quality Gate Summary"
$lines += "Generated: $($summary.generated)"
$lines += "EnforceKpi: $($summary.enforce_kpi)"
$lines += ""
$lines += "Gate Results"
$lines += "- GateA (conversion and load): $($summary.gates.gate_a_conversion_and_load)"
$lines += "- GateB (diag contract): $($summary.gates.gate_b_diag_contract)"
$lines += "- GateC (strict allowlist smoke): $($summary.gates.gate_c_strict_allowlist_smoke)"
$lines += "- GateQ (quality): $($summary.gates.gate_q_quality)"
$lines += "- GateE (profile kpi): $($summary.gates.gate_e_profile_kpi)"
$lines += "- GateD (kpi): $($summary.gates.gate_d_kpi)"
$lines += "- Overall: $($summary.gates.overall)"
$lines += ""
$lines += "Rows"
foreach ($row in $rows) {
    $lines += "- $($row.name): status=$($row.status), compat=$($row.compat), stage=$($row.parser_stage), primary=$($row.primary_error), sections=$($row.section_count), compressed=$($row.compressed_sections), quality=$($row.quality_ok), profileRed=$($row.profile_reduction_pct)%, sizeRed=$($row.size_reduction_pct)%, writeRed=$($row.write_reduction_pct)%, bufferRed=$($row.buffer_reduction_pct)%, profileKpi=$($row.profile_kpi_ok), kpi=$($row.kpi_ok)"
}
$lines += ""
$lines += "Artifacts"
$lines += "- summary_json=$summaryJsonPath"
$lines += "- summary_txt=$summaryTxtPath"
$lines | Set-Content -Path $summaryTxtPath -Encoding UTF8

if (-not $overall) { exit 1 }
exit 0
