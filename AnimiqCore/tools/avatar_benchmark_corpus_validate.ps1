param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string[]]$SupportedExtensions = @("vrm", "miq"),
    [int]$MinSamples = 30,
    [string]$OutputJson = ".\build\reports\avatar_benchmark_corpus_validation.json",
    [string]$OutputTxt = ".\build\reports\avatar_benchmark_corpus_validation.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path)
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

if (-not (Test-Path $ManifestPath)) {
    throw "ManifestPath not found: $ManifestPath"
}

$SupportedExtensions = Normalize-Extensions -Extensions $SupportedExtensions
if ($SupportedExtensions.Count -eq 0) {
    throw "SupportedExtensions must not be empty"
}

$manifestAbs = Resolve-AbsolutePath -Path $ManifestPath
$manifest = Get-Content -Path $manifestAbs -Raw | ConvertFrom-Json
if ($null -eq $manifest -or $null -eq $manifest.samples) {
    throw "manifest schema invalid: expected root.samples[]"
}

$rows = [System.Collections.Generic.List[object]]::new()
$missing = [System.Collections.Generic.List[object]]::new()
$unsupported = [System.Collections.Generic.List[object]]::new()
$duplicateIds = [System.Collections.Generic.List[string]]::new()
$extCounts = @{}
$ids = @{}

foreach ($sample in $manifest.samples) {
    $id = "$(Get-SampleValue -Sample $sample -Keys @('id'))".Trim()
    if ([string]::IsNullOrWhiteSpace($id)) {
        continue
    }
    if ($ids.ContainsKey($id)) {
        $duplicateIds.Add($id)
    } else {
        $ids[$id] = $true
    }

    $pathRaw = "$(Get-SampleValue -Sample $sample -Keys @('path'))"
    $pathAbs = Resolve-AbsolutePath -Path $pathRaw
    $ext = [System.IO.Path]::GetExtension($pathAbs).TrimStart('.').ToLowerInvariant()
    if ([string]::IsNullOrWhiteSpace($ext)) {
        $ext = "unknown"
    }
    if (-not $extCounts.ContainsKey($ext)) {
        $extCounts[$ext] = 0
    }
    $extCounts[$ext] = [int]$extCounts[$ext] + 1

    $exists = Test-Path $pathAbs
    if (-not $exists) {
        $missing.Add([PSCustomObject]@{
            id = $id
            path = $pathAbs
            reason = "missing"
        })
    }
    if ($SupportedExtensions -notcontains $ext) {
        $unsupported.Add([PSCustomObject]@{
            id = $id
            extension = $ext
            path = $pathAbs
            reason = "unsupported_extension"
        })
    }
    $rows.Add([PSCustomObject]@{
        id = $id
        path = $pathAbs
        extension = $ext
        exists = $exists
    })
}

$validRows = @($rows | Where-Object { $_.exists -and ($SupportedExtensions -contains $_.extension) })
$validation = [PSCustomObject]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    manifest_path = $manifestAbs
    supported_extensions = @($SupportedExtensions)
    minimum_samples = $MinSamples
    totals = [PSCustomObject]@{
        manifest_rows = $rows.Count
        valid_rows = $validRows.Count
        missing_rows = $missing.Count
        unsupported_rows = $unsupported.Count
        duplicate_ids = $duplicateIds.Count
    }
    by_extension = $extCounts
    missing = $missing
    unsupported = $unsupported
    duplicate_ids = $duplicateIds
    valid_rows = $validRows
}

$outJsonAbs = Resolve-AbsolutePath -Path $OutputJson
$outTxtAbs = Resolve-AbsolutePath -Path $OutputTxt
foreach ($p in @($outJsonAbs, $outTxtAbs)) {
    $dir = Split-Path -Parent $p
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
}

$validation | ConvertTo-Json -Depth 8 | Set-Content -Path $outJsonAbs -Encoding UTF8

$lines = @()
$lines += "Avatar Benchmark Corpus Validation"
$lines += "GeneratedUTC: $($validation.generated_utc)"
$lines += "ManifestPath: $($validation.manifest_path)"
$lines += "SupportedExtensions: $($validation.supported_extensions -join ',')"
$lines += "MinimumSamples: $($validation.minimum_samples)"
$lines += "Totals: manifest=$($validation.totals.manifest_rows), valid=$($validation.totals.valid_rows), missing=$($validation.totals.missing_rows), unsupported=$($validation.totals.unsupported_rows), dupId=$($validation.totals.duplicate_ids)"
$lines += ""
$lines += "ByExtension"
foreach ($k in @($validation.by_extension.Keys | Sort-Object)) {
    $lines += "- .${k}: $($validation.by_extension[$k])"
}
if ($missing.Count -gt 0) {
    $lines += ""
    $lines += "Missing"
    foreach ($m in $missing) {
        $lines += "- $($m.id): $($m.path)"
    }
}
if ($unsupported.Count -gt 0) {
    $lines += ""
    $lines += "Unsupported"
    foreach ($u in $unsupported) {
        $lines += "- $($u.id): .$($u.extension) path=$($u.path)"
    }
}
$lines | Set-Content -Path $outTxtAbs -Encoding UTF8

Write-Host "json=$outJsonAbs"
Write-Host "txt=$outTxtAbs"

if ($duplicateIds.Count -gt 0) {
    throw "duplicate sample ids found: $($duplicateIds -join ',')"
}
if ($missing.Count -gt 0) {
    throw "missing sample files found: $($missing.Count)"
}
if ($unsupported.Count -gt 0) {
    throw "unsupported extension rows found: $($unsupported.Count)"
}
if ($validRows.Count -lt $MinSamples) {
    throw "valid sample count below minimum: valid=$($validRows.Count) min=$MinSamples"
}
