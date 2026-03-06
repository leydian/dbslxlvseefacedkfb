param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$AvatarToolPath = ".\build\Release\avatar_tool.exe",
    [string]$VSeeFaceObservationPath = "",
    [string]$OutputJson = ".\build\reports\avatar_differential_benchmark_summary.json",
    [string]$OutputTxt = ".\build\reports\avatar_differential_benchmark_summary.txt",
    [string]$ErrorTaxonomyJson = ".\build\reports\avatar_differential_error_taxonomy.json",
    [string]$ParityDashboardMd = ".\build\reports\avatar_parity_dashboard.md",
    [int]$WarningDebtThreshold = 5,
    [string]$GateProfile = "strict",
    [string[]]$SupportedExtensions = @("vrm", "miq")
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ""
    }
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function Get-SampleValue {
    param([object]$Sample, [string[]]$Keys, $DefaultValue = $null)
    foreach ($k in $Keys) {
        $p = $Sample.PSObject.Properties[$k]
        if ($null -ne $p -and $null -ne $p.Value -and -not [string]::IsNullOrWhiteSpace("$($p.Value)")) {
            return $p.Value
        }
    }
    return $DefaultValue
}

function To-BoolOrNull {
    param($Value)
    if ($null -eq $Value) {
        return $null
    }
    return [bool]$Value
}

function Normalize-Extensions {
    param([string[]]$Extensions)
    $set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($raw in $Extensions) {
        if ($null -eq $raw) {
            continue
        }
        foreach ($part in ("$raw" -split ",")) {
            $e = $part.Trim().TrimStart(".").ToLowerInvariant()
            if (-not [string]::IsNullOrWhiteSpace($e)) {
                $set.Add($e) | Out-Null
            }
        }
    }
    return @($set)
}

function Increment-Map {
    param([hashtable]$Map, [string]$Key, [int]$Delta = 1)
    if ([string]::IsNullOrWhiteSpace($Key)) {
        return
    }
    if ($Map.ContainsKey($Key)) {
        $Map[$Key] = [int]$Map[$Key] + $Delta
    } else {
        $Map[$Key] = $Delta
    }
}

function Top-MapEntries {
    param([hashtable]$Map, [int]$Top = 10)
    return @(
        $Map.GetEnumerator() |
            Sort-Object Value -Descending |
            Select-Object -First $Top |
            ForEach-Object {
                [PSCustomObject]@{
                    key = "$($_.Key)"
                    count = [int]$_.Value
                }
            }
    )
}

if (-not (Test-Path $ManifestPath)) {
    throw "ManifestPath not found: $ManifestPath"
}
if (-not (Test-Path $AvatarToolPath)) {
    throw "AvatarToolPath not found: $AvatarToolPath"
}

$SupportedExtensions = Normalize-Extensions -Extensions $SupportedExtensions
if ($SupportedExtensions.Count -eq 0) {
    throw "SupportedExtensions must not be empty"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$manifestAbs = Resolve-AbsolutePath -Path $ManifestPath -BaseDirectory $repoRoot
$avatarToolAbs = Resolve-AbsolutePath -Path $AvatarToolPath -BaseDirectory $repoRoot
$manifest = Get-Content -Path $manifestAbs -Raw | ConvertFrom-Json
if ($null -eq $manifest -or $null -eq $manifest.samples) {
    throw "manifest schema invalid: expected root.samples[]"
}

$vseeRowsById = @{}
if (-not [string]::IsNullOrWhiteSpace($VSeeFaceObservationPath)) {
    $obsAbs = Resolve-AbsolutePath -Path $VSeeFaceObservationPath -BaseDirectory $repoRoot
    if (-not (Test-Path $obsAbs)) {
        throw "VSeeFaceObservationPath not found: $obsAbs"
    }
    $obs = Get-Content -Path $obsAbs -Raw | ConvertFrom-Json
    if ($null -ne $obs -and $null -ne $obs.rows) {
        foreach ($row in $obs.rows) {
            $id = "$($row.id)".Trim()
            if (-not [string]::IsNullOrWhiteSpace($id)) {
                $vseeRowsById[$id] = $row
            }
        }
    }
}

$rows = [System.Collections.Generic.List[object]]::new()
$parityRows = [System.Collections.Generic.List[object]]::new()
$excludedRows = [System.Collections.Generic.List[object]]::new()
$p0 = 0
$p1 = 0
$p2 = 0
$pass = 0
$priorityByExtension = @{}
$reasonCounts = @{}
$primaryErrorCounts = @{}
$warningCodeCounts = @{}

foreach ($sample in $manifest.samples) {
    $id = "$(Get-SampleValue -Sample $sample -Keys @('id'))".Trim()
    if ([string]::IsNullOrWhiteSpace($id)) {
        continue
    }

    $sampleClass = "$(Get-SampleValue -Sample $sample -Keys @('sampleClass','sample_class') -DefaultValue 'unknown')"
    $mustRenderVisible = [bool](Get-SampleValue -Sample $sample -Keys @('mustRenderVisible','must_render_visible') -DefaultValue $false)
    $samplePathRaw = "$(Get-SampleValue -Sample $sample -Keys @('path'))"
    $samplePathAbs = Resolve-AbsolutePath -Path $samplePathRaw -BaseDirectory $repoRoot
    $ext = [System.IO.Path]::GetExtension($samplePathAbs).TrimStart('.').ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($ext)) {
        $ext = "unknown"
    }
    if ($SupportedExtensions -notcontains $ext) {
        $excludedRows.Add([PSCustomObject]@{
            id = $id
            sample_class = $sampleClass
            sample_path = $samplePathAbs
            extension = $ext
            reason = "unsupported_extension_excluded"
        })
        continue
    }
    if (-not $priorityByExtension.ContainsKey($ext)) {
        $priorityByExtension[$ext] = [PSCustomObject]@{ total = 0; p0 = 0; p1 = 0; p2 = 0; pass = 0 }
    }

    $vseeRow = $null
    if ($vseeRowsById.ContainsKey($id)) {
        $vseeRow = $vseeRowsById[$id]
    }
    $vseeLoadOk = if ($null -ne $vseeRow) { To-BoolOrNull $vseeRow.load_ok } else { $null }
    $vseeRenderVisible = if ($null -ne $vseeRow) { To-BoolOrNull $vseeRow.render_visible } else { $null }
    $vseeCrash = if ($null -ne $vseeRow) { To-BoolOrNull $vseeRow.crash_or_freeze } else { $null }
    $vseeElapsedMs = if ($null -ne $vseeRow -and $null -ne $vseeRow.elapsed_ms) { [int]$vseeRow.elapsed_ms } else { $null }
    $vseeRuntimeReady = if ($null -eq $vseeLoadOk) { $null } else { [bool]($vseeLoadOk -eq $true -and $vseeCrash -ne $true) }

    $animiqLoadOk = $false
    $animiqStage = "unknown"
    $animiqPrimary = "UNKNOWN"
    $animiqCritical = 0
    $animiqWarningCodeCount = 0
    $animiqRenderVisible = $false
    $animiqCompat = "unknown"
    $animiqWarningCodes = @()
    $animiqElapsedMs = 0
    $exitCode = -1
    $rawOut = @()

    if (-not (Test-Path $samplePathAbs)) {
        $animiqPrimary = "FILE_NOT_FOUND"
        $animiqStage = "missing-file"
    } else {
        $tmpJson = Join-Path $env:TEMP ("animiq_benchmark_" + [Guid]::NewGuid().ToString("N") + ".json")
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $rawOut = & $avatarToolAbs $samplePathAbs "--json-out=$tmpJson" 2>&1
        $exitCode = $LASTEXITCODE
        $sw.Stop()
        $animiqElapsedMs = [int]$sw.ElapsedMilliseconds

        $animiq = $null
        if (Test-Path $tmpJson) {
            $animiq = Get-Content -Path $tmpJson -Raw | ConvertFrom-Json
            Remove-Item -Path $tmpJson -Force -ErrorAction SilentlyContinue
        }

        if ($null -ne $animiq) {
            $animiqLoadOk = [bool]$animiq.loadSucceeded
            $animiqStage = "$($animiq.parserStage)"
            $animiqPrimary = "$($animiq.primaryError)"
            $animiqCompat = "$($animiq.compat)"
            $animiqCritical = [int]$animiq.counts.criticalWarningCount
            $animiqWarningCodeCount = [int]$animiq.counts.warningCodeCount
            $animiqRenderVisible = [bool]$animiq.renderVisibleHeuristic
            if ($null -ne $animiq.warningCodes) {
                foreach ($c in $animiq.warningCodes) {
                    $code = "$c".Trim()
                    if (-not [string]::IsNullOrWhiteSpace($code)) {
                        $animiqWarningCodes += $code
                        Increment-Map -Map $warningCodeCounts -Key $code
                    }
                }
            }
        } else {
            $animiqPrimary = "ANIMIQ_JSON_MISSING"
            $animiqStage = "failed"
            $animiqLoadOk = $false
            $animiqRenderVisible = $false
        }
    }

    $animiqRuntimeReady = [bool]($animiqLoadOk -and $animiqStage -eq "runtime-ready" -and $animiqPrimary -eq "NONE")

    if ($animiqPrimary -ne "NONE") {
        Increment-Map -Map $primaryErrorCounts -Key $animiqPrimary
    }

    $priority = "PASS"
    $reason = "pass"
    if ($vseeLoadOk -eq $true) {
        if ((-not $animiqRuntimeReady) -or ($vseeRenderVisible -eq $true -and -not $animiqRenderVisible)) {
            $priority = "P0"
            $reason = "vseeface_ok_animiq_not_equivalent"
        } elseif ($animiqCompat -eq "partial" -or $animiqCritical -gt 0 -or ($mustRenderVisible -and -not $animiqRenderVisible)) {
            $priority = "P1"
            $reason = "animiq_quality_gap_after_load"
        } elseif ($animiqWarningCodeCount -gt $WarningDebtThreshold) {
            $priority = "P2"
            $reason = "warning_debt_high"
        }
    } else {
        if (-not (Test-Path $samplePathAbs)) {
            $priority = "P0"
            $reason = "sample_file_missing"
        } elseif ($animiqWarningCodeCount -gt $WarningDebtThreshold) {
            $priority = "P2"
            $reason = "warning_debt_high"
        }
    }

    switch ($priority) {
        "P0" { $p0++ }
        "P1" { $p1++ }
        "P2" { $p2++ }
        default { $pass++ }
    }

    Increment-Map -Map $reasonCounts -Key $reason
    $extBucket = $priorityByExtension[$ext]
    $extBucket.total = [int]$extBucket.total + 1
    switch ($priority) {
        "P0" { $extBucket.p0 = [int]$extBucket.p0 + 1 }
        "P1" { $extBucket.p1 = [int]$extBucket.p1 + 1 }
        "P2" { $extBucket.p2 = [int]$extBucket.p2 + 1 }
        default { $extBucket.pass = [int]$extBucket.pass + 1 }
    }

    $row = [PSCustomObject]@{
        id = $id
        sample_class = $sampleClass
        sample_path = $samplePathAbs
        extension = $ext
        must_render_visible = $mustRenderVisible
        animiq_load_ok = $animiqLoadOk
        animiq_runtime_ready = $animiqRuntimeReady
        animiq_parser_stage = $animiqStage
        animiq_primary_error = $animiqPrimary
        animiq_compat = $animiqCompat
        animiq_critical_warning_count = $animiqCritical
        animiq_warning_code_count = $animiqWarningCodeCount
        animiq_warning_codes = $animiqWarningCodes
        animiq_render_visible = $animiqRenderVisible
        animiq_exit_code = $exitCode
        animiq_elapsed_ms = $animiqElapsedMs
        vseeface_load_ok = $vseeLoadOk
        vseeface_runtime_ready = $vseeRuntimeReady
        vseeface_render_visible = $vseeRenderVisible
        vseeface_crash_or_freeze = $vseeCrash
        vseeface_elapsed_ms = $vseeElapsedMs
        priority = $priority
        reason = $reason
        raw_head = if ($rawOut.Count -gt 0) { "$($rawOut[0])" } else { "" }
    }
    $rows.Add($row)

    $parityRows.Add([PSCustomObject]@{
        id = $id
        sample_class = $sampleClass
        engine = "animiq"
        load_ok = $animiqLoadOk
        runtime_ready = $animiqRuntimeReady
        visible = $animiqRenderVisible
        primary_error = $animiqPrimary
        parser_stage = $animiqStage
        warning_codes = $animiqWarningCodes
        critical_warning_count = $animiqCritical
        elapsed_ms = $animiqElapsedMs
        crash_or_freeze = $false
    })

    if ($null -ne $vseeRow) {
        $parityRows.Add([PSCustomObject]@{
            id = $id
            sample_class = $sampleClass
            engine = "vseeface"
            load_ok = $vseeLoadOk
            runtime_ready = $vseeRuntimeReady
            visible = $vseeRenderVisible
            primary_error = if ($vseeLoadOk -eq $true) { "NONE" } elseif ($vseeCrash -eq $true) { "CRASH_OR_FREEZE" } else { "LOAD_FAILED" }
            parser_stage = if ($vseeLoadOk -eq $true) { "runtime-ready" } else { "failed" }
            warning_codes = @()
            critical_warning_count = 0
            elapsed_ms = $vseeElapsedMs
            crash_or_freeze = $vseeCrash
        })
    }
}

$summary = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    gate_profile = $GateProfile
    warning_debt_threshold = $WarningDebtThreshold
    supported_extensions = @($SupportedExtensions)
    manifest_path = $manifestAbs
    avatar_tool_path = $avatarToolAbs
    vseeface_observation_path = if ([string]::IsNullOrWhiteSpace($VSeeFaceObservationPath)) { "" } else { (Resolve-AbsolutePath -Path $VSeeFaceObservationPath -BaseDirectory $repoRoot) }
    total = $rows.Count
    pass = $pass
    excluded_rows = $excludedRows.Count
    priority = [PSCustomObject]@{
        p0 = $p0
        p1 = $p1
        p2 = $p2
        pass = $pass
    }
    by_extension = $priorityByExtension
    top_reasons = @(Top-MapEntries -Map $reasonCounts -Top 10)
    top_primary_errors = @(Top-MapEntries -Map $primaryErrorCounts -Top 10)
    top_warning_codes = @(Top-MapEntries -Map $warningCodeCounts -Top 20)
    excluded = $excludedRows
    parity_rows = $parityRows
    rows = $rows
}

$errorTaxonomy = [PSCustomObject]@{
    generated_utc = $summary.generated_utc
    total = $rows.Count
    priority = $summary.priority
    by_reason = @(Top-MapEntries -Map $reasonCounts -Top 100)
    by_primary_error = @(Top-MapEntries -Map $primaryErrorCounts -Top 100)
    by_warning_code = @(Top-MapEntries -Map $warningCodeCounts -Top 200)
}

$outputJsonAbs = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot
$outputTxtAbs = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
$taxonomyAbs = Resolve-AbsolutePath -Path $ErrorTaxonomyJson -BaseDirectory $repoRoot
$dashboardAbs = Resolve-AbsolutePath -Path $ParityDashboardMd -BaseDirectory $repoRoot

@($outputJsonAbs, $outputTxtAbs, $taxonomyAbs, $dashboardAbs) |
    ForEach-Object {
        $dir = Split-Path -Parent $_
        if (-not (Test-Path $dir)) {
            New-Item -ItemType Directory -Path $dir | Out-Null
        }
    }

$summary | ConvertTo-Json -Depth 10 | Set-Content -Path $outputJsonAbs -Encoding UTF8
$errorTaxonomy | ConvertTo-Json -Depth 8 | Set-Content -Path $taxonomyAbs -Encoding UTF8

$lines = @()
$lines += "Avatar Differential Benchmark Summary"
$lines += "GeneratedUTC: $($summary.generated_utc)"
$lines += "GateProfile: $($summary.gate_profile)"
$lines += "SupportedExtensions: $($summary.supported_extensions -join ',')"
$lines += "ManifestPath: $($summary.manifest_path)"
$lines += "AvatarToolPath: $($summary.avatar_tool_path)"
$lines += "VSeeFaceObservationPath: $($summary.vseeface_observation_path)"
$lines += "Total: $($summary.total)"
$lines += "ExcludedRows: $($summary.excluded_rows)"
$lines += "Priority: P0=$($summary.priority.p0), P1=$($summary.priority.p1), P2=$($summary.priority.p2), PASS=$($summary.priority.pass)"
$lines += ""
$lines += "ByExtension"
foreach ($k in @($summary.by_extension.Keys | Sort-Object)) {
    $b = $summary.by_extension[$k]
    $lines += "- .$k total=$($b.total), p0=$($b.p0), p1=$($b.p1), p2=$($b.p2), pass=$($b.pass)"
}
$lines += ""
$lines += "Rows"
foreach ($row in $rows) {
    $lines += "- [$($row.priority)] $($row.id) ext=.$($row.extension) class=$($row.sample_class)"
    $lines += "  animiq(load=$($row.animiq_load_ok), ready=$($row.animiq_runtime_ready), stage=$($row.animiq_parser_stage), primary=$($row.animiq_primary_error), compat=$($row.animiq_compat), critical=$($row.animiq_critical_warning_count), visible=$($row.animiq_render_visible), ms=$($row.animiq_elapsed_ms))"
    $lines += "  vseeface(load=$($row.vseeface_load_ok), ready=$($row.vseeface_runtime_ready), visible=$($row.vseeface_render_visible), crash=$($row.vseeface_crash_or_freeze), ms=$($row.vseeface_elapsed_ms))"
    $lines += "  reason=$($row.reason)"
}
$lines | Set-Content -Path $outputTxtAbs -Encoding UTF8

$md = @()
$md += "# Avatar Parity Dashboard"
$md += ""
$md += "- Generated UTC: $($summary.generated_utc)"
$md += "- Gate Profile: $($summary.gate_profile)"
$md += "- Supported extensions: $($summary.supported_extensions -join ',')"
$md += "- Manifest: ``$($summary.manifest_path)``"
$md += "- VSeeFace observations: ``$($summary.vseeface_observation_path)``"
$md += "- Excluded rows: $($summary.excluded_rows)"
$md += ""
$md += "## Priority Summary"
$md += ""
$md += "| Total | PASS | P0 | P1 | P2 |"
$md += "|---:|---:|---:|---:|---:|"
$md += "| $($summary.total) | $($summary.priority.pass) | $($summary.priority.p0) | $($summary.priority.p1) | $($summary.priority.p2) |"
$md += ""
$md += "## Extension Breakdown"
$md += ""
$md += "| Extension | Total | PASS | P0 | P1 | P2 |"
$md += "|---|---:|---:|---:|---:|---:|"
foreach ($k in @($summary.by_extension.Keys | Sort-Object)) {
    $b = $summary.by_extension[$k]
    $md += "| .$k | $($b.total) | $($b.pass) | $($b.p0) | $($b.p1) | $($b.p2) |"
}
$md += ""
$md += "## Top Primary Errors"
$md += ""
$md += "| Error | Count |"
$md += "|---|---:|"
foreach ($e in $summary.top_primary_errors) {
    $md += "| $($e.key) | $($e.count) |"
}
$md += ""
$md += "## Top Warning Codes"
$md += ""
$md += "| WarningCode | Count |"
$md += "|---|---:|"
foreach ($w in $summary.top_warning_codes) {
    $md += "| $($w.key) | $($w.count) |"
}
$md += ""
$md += "## P0 Samples"
$md += ""
$md += "| Id | Ext | Reason | AnimiqPrimary | AnimiqStage | VSeeFaceLoad |"
$md += "|---|---|---|---|---|---|"
foreach ($r in @($rows | Where-Object { $_.priority -eq 'P0' })) {
    $md += "| $($r.id) | .$($r.extension) | $($r.reason) | $($r.animiq_primary_error) | $($r.animiq_parser_stage) | $($r.vseeface_load_ok) |"
}
$md | Set-Content -Path $dashboardAbs -Encoding UTF8

Write-Host "json=$outputJsonAbs"
Write-Host "txt=$outputTxtAbs"
Write-Host "taxonomy=$taxonomyAbs"
Write-Host "dashboard=$dashboardAbs"
