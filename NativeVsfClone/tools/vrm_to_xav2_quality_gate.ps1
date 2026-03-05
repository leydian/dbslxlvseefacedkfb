param(
    [string]$SampleDir = ".\sample",
    [string]$VrmToXav2Path = ".\build\Release\vrm_to_xav2.exe",
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
    $picked = @()
    foreach ($name in $Allowlist) {
        $p = Join-Path $Dir $name
        if (Test-Path -LiteralPath $p) {
            $picked += (Get-Item -LiteralPath $p)
        }
    }
    if ($picked.Count -eq 0) {
        $picked = Get-ChildItem -Path $Dir -Filter *.vrm -File | Sort-Object Name | Select-Object -First $MaxCount
    } else {
        $picked = $picked | Select-Object -First $MaxCount
    }
    return $picked
}

if (-not (Test-Path -LiteralPath $VrmToXav2Path)) { throw "vrm_to_xav2 not found: $VrmToXav2Path" }
if (-not (Test-Path -LiteralPath $AvatarToolPath)) { throw "avatar_tool not found: $AvatarToolPath" }
if (-not (Test-Path -LiteralPath $SampleDir)) { throw "sample dir not found: $SampleDir" }

New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$outDir = Join-Path $ReportDir "vrm_to_xav2_gate_outputs"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$allowPath = Join-Path $ReportDir "vrm_to_xav2_strict_allowlist.txt"
@(
    "# strict allowlist example"
    "XAV2_PHYSICS_REF_MISSING"
    "XAV2_MATERIAL_TYPED_TEXTURE_UNRESOLVED"
) | Set-Content -Path $allowPath -Encoding UTF8

$samples = Pick-Samples -Dir $SampleDir -Allowlist $SampleAllowlist -MaxCount $MaxSamples
if ($samples.Count -eq 0) { throw "no vrm samples found under $SampleDir" }

$rows = [System.Collections.Generic.List[object]]::new()
$gateA = $true # conversion/load pass
$gateB = $true # diagnostics contract
$gateC = $true # strict allowlist smoke
$gateD = $true # kpi

foreach ($sample in $samples) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($sample.Name)
    $compressedOut = Join-Path $outDir ($base + ".xav2")
    $rawOut = Join-Path $outDir ($base + ".raw.xav2")
    $diagPath = Join-Path $outDir ($base + ".diag.json")
    $perfPath = Join-Path $outDir ($base + ".perf.json")
    $rawDiagPath = Join-Path $outDir ($base + ".raw.diag.json")
    $rawPerfPath = Join-Path $outDir ($base + ".raw.perf.json")

    & $VrmToXav2Path --diag-json $diagPath --perf-metrics-json $perfPath $sample.FullName $compressedOut | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $gateA = $false
        $rows.Add([PSCustomObject]@{ name = $sample.Name; status = "FAIL"; reason = "compressed convert failed" })
        continue
    }
    & $VrmToXav2Path --no-compress --diag-json $rawDiagPath --perf-metrics-json $rawPerfPath $sample.FullName $rawOut | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $gateA = $false
        $rows.Add([PSCustomObject]@{ name = $sample.Name; status = "FAIL"; reason = "raw convert failed" })
        continue
    }

    $probeLines = & $AvatarToolPath $compressedOut
    $probe = Parse-AvatarToolOutput -Lines $probeLines
    $probeOk = ($probe.Compat -eq "full") -and ($probe.ParserStage -eq "runtime-ready") -and ($probe.PrimaryError -eq "NONE")
    if (-not $probeOk) {
        $gateA = $false
    }

    if (-not (Test-Path -LiteralPath $diagPath) -or -not (Test-Path -LiteralPath $perfPath)) {
        $gateB = $false
    }
    $diag = if (Test-Path -LiteralPath $diagPath) { Get-Content -Raw -Path $diagPath | ConvertFrom-Json } else { $null }
    $perf = if (Test-Path -LiteralPath $perfPath) { Get-Content -Raw -Path $perfPath | ConvertFrom-Json } else { $null }
    $rawPerf = if (Test-Path -LiteralPath $rawPerfPath) { Get-Content -Raw -Path $rawPerfPath | ConvertFrom-Json } else { $null }

    if ($null -eq $diag -or $null -eq $perf -or $null -eq $rawPerf) {
        $gateB = $false
        $rows.Add([PSCustomObject]@{ name = $sample.Name; status = "FAIL"; reason = "missing diag/perf artifacts" })
        continue
    }
    if (($diag.sectionCount -le 0) -or ($diag.rawTotalBytes -le 0) -or ($diag.writtenTotalBytes -le 0)) {
        $gateB = $false
    }

    & $VrmToXav2Path --strict --strict-allowlist $allowPath $sample.FullName (Join-Path $outDir ($base + ".strict.xav2")) | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $gateC = $false
    }

    $sizeReduction = 0.0
    $writeReduction = 0.0
    $bufferReduction = 0.0
    if ([double]$rawPerf.writtenTotalBytes -gt 0) {
        $sizeReduction = (1.0 - ([double]$perf.writtenTotalBytes / [double]$rawPerf.writtenTotalBytes)) * 100.0
    }
    if ([double]$rawPerf.timingMs.write -gt 0) {
        $writeReduction = (1.0 - ([double]$perf.timingMs.write / [double]$rawPerf.timingMs.write)) * 100.0
    }
    if ([double]$rawPerf.maxPayloadBufferBytes -gt 0) {
        $bufferReduction = (1.0 - ([double]$perf.maxPayloadBufferBytes / [double]$rawPerf.maxPayloadBufferBytes)) * 100.0
    }

    $kpiOk = ($sizeReduction -ge $TargetSizeReductionPercent) -and
             ($writeReduction -ge $TargetWriteTimeReductionPercent) -and
             ($bufferReduction -ge $TargetBufferReductionPercent)
    if ($EnforceKpi -and (-not $kpiOk)) {
        $gateD = $false
    }

    $rows.Add([PSCustomObject]@{
        name = $sample.Name
        status = if ($probeOk) { "PASS" } else { "FAIL" }
        compat = $probe.Compat
        parser_stage = $probe.ParserStage
        primary_error = $probe.PrimaryError
        section_count = [int]$diag.sectionCount
        compressed_sections = [int]$diag.compressedSectionCount
        size_reduction_pct = [Math]::Round($sizeReduction, 2)
        write_reduction_pct = [Math]::Round($writeReduction, 2)
        buffer_reduction_pct = [Math]::Round($bufferReduction, 2)
        kpi_ok = $kpiOk
    })
}

$overall = $gateA -and $gateB -and $gateC -and (($EnforceKpi -and $gateD) -or (-not $EnforceKpi))
$summary = [ordered]@{
    generated = (Get-Date).ToString("s")
    enforce_kpi = [bool]$EnforceKpi
    kpi_targets = [ordered]@{
        size_reduction_percent = $TargetSizeReductionPercent
        write_time_reduction_percent = $TargetWriteTimeReductionPercent
        buffer_reduction_percent = $TargetBufferReductionPercent
    }
    gates = [ordered]@{
        gate_a_conversion_and_load = if ($gateA) { "PASS" } else { "FAIL" }
        gate_b_diag_contract = if ($gateB) { "PASS" } else { "FAIL" }
        gate_c_strict_allowlist_smoke = if ($gateC) { "PASS" } else { "FAIL" }
        gate_d_kpi = if ($gateD) { "PASS" } else { "FAIL" }
        overall = if ($overall) { "PASS" } else { "FAIL" }
    }
    rows = $rows
}

$summaryJsonPath = Join-Path $ReportDir "vrm_to_xav2_quality_gate_summary.json"
$summaryTxtPath = Join-Path $ReportDir "vrm_to_xav2_quality_gate_summary.txt"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryJsonPath -Encoding UTF8

$lines = @()
$lines += "VRM_TO_XAV2 Quality Gate Summary"
$lines += "Generated: $($summary.generated)"
$lines += "EnforceKpi: $($summary.enforce_kpi)"
$lines += ""
$lines += "Gate Results"
$lines += "- GateA (conversion and load): $($summary.gates.gate_a_conversion_and_load)"
$lines += "- GateB (diag contract): $($summary.gates.gate_b_diag_contract)"
$lines += "- GateC (strict allowlist smoke): $($summary.gates.gate_c_strict_allowlist_smoke)"
$lines += "- GateD (kpi): $($summary.gates.gate_d_kpi)"
$lines += "- Overall: $($summary.gates.overall)"
$lines += ""
$lines += "Rows"
foreach ($row in $rows) {
    $lines += "- $($row.name): status=$($row.status), compat=$($row.compat), stage=$($row.parser_stage), primary=$($row.primary_error), sections=$($row.section_count), compressed=$($row.compressed_sections), sizeRed=$($row.size_reduction_pct)%, writeRed=$($row.write_reduction_pct)%, bufferRed=$($row.buffer_reduction_pct)%, kpi=$($row.kpi_ok)"
}
$lines += ""
$lines += "Artifacts"
$lines += "- summary_json=$summaryJsonPath"
$lines += "- summary_txt=$summaryTxtPath"
$lines | Set-Content -Path $summaryTxtPath -Encoding UTF8

if (-not $overall) { exit 1 }
exit 0
