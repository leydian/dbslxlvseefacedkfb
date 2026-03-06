param(
    [string]$RootDir = ".",
    [string[]]$SupportedExtensions = @("vrm", "miq"),
    [int]$MaxSamples = 30,
    [switch]$Recurse,
    [string]$OutputManifest = ".\build\reports\avatar_corpus_snapshot_manifest.json",
    [string]$OutputTxt = ".\build\reports\avatar_corpus_snapshot_manifest.txt"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath($Path)
}

function New-SampleId {
    param([string]$Extension, [int]$Index)
    return ("{0}_snapshot_{1}" -f $Extension, $Index.ToString("00"))
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

$rootAbs = Resolve-AbsolutePath -Path $RootDir
if (-not (Test-Path $rootAbs)) {
    throw "RootDir not found: $RootDir"
}

$SupportedExtensions = Normalize-Extensions -Extensions $SupportedExtensions
if ($SupportedExtensions.Count -eq 0) {
    throw "SupportedExtensions must not be empty"
}

$allRows = [System.Collections.Generic.List[object]]::new()
foreach ($ext in $SupportedExtensions) {
    $pattern = "*.$ext"
    $files = if ($Recurse.IsPresent) {
        Get-ChildItem -Path $rootAbs -File -Filter $pattern -Recurse -ErrorAction SilentlyContinue
    } else {
        Get-ChildItem -Path $rootAbs -File -Filter $pattern -ErrorAction SilentlyContinue
    }
    foreach ($f in ($files | Sort-Object FullName)) {
        $allRows.Add([PSCustomObject]@{
            extension = $ext
            path = $f.FullName
            name = $f.Name
        })
    }
}

$selected = @($allRows | Select-Object -First $MaxSamples)
$samples = [System.Collections.Generic.List[object]]::new()
$idxByExt = @{}
foreach ($row in $selected) {
    if (-not $idxByExt.ContainsKey($row.extension)) {
        $idxByExt[$row.extension] = 0
    }
    $idxByExt[$row.extension] = [int]$idxByExt[$row.extension] + 1
    $sampleId = New-SampleId -Extension $row.extension -Index $idxByExt[$row.extension]
    $sourcePath = if ($row.extension -eq "miq") { "miq_vrm_origin" } else { "vrm" }
    $samples.Add([PSCustomObject]@{
        id = $sampleId
        name = $row.name
        path = $row.path
        sampleClass = "boundary"
        mustRenderVisible = $true
        source_path = $sourcePath
    })
}

$manifest = [PSCustomObject]@{
    version = "1.2"
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    root_dir = $rootAbs
    recurse = $Recurse.IsPresent
    supported_extensions = @($SupportedExtensions)
    max_samples = $MaxSamples
    total_selected = $samples.Count
    samples = $samples
}

$outManifestAbs = Resolve-AbsolutePath -Path $OutputManifest
$outTxtAbs = Resolve-AbsolutePath -Path $OutputTxt
foreach ($p in @($outManifestAbs, $outTxtAbs)) {
    $dir = Split-Path -Parent $p
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir | Out-Null
    }
}

$manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $outManifestAbs -Encoding UTF8

$lines = @()
$lines += "Avatar Corpus Snapshot Manifest"
$lines += "GeneratedUTC: $($manifest.generated_utc)"
$lines += "RootDir: $($manifest.root_dir)"
$lines += "Recurse: $($manifest.recurse)"
$lines += "SupportedExtensions: $($manifest.supported_extensions -join ',')"
$lines += "MaxSamples/Selected: $($manifest.max_samples)/$($manifest.total_selected)"
$lines += ""
$lines += "Samples"
foreach ($s in $samples) {
    $lines += "- $($s.id) .$([System.IO.Path]::GetExtension($s.path).TrimStart('.')) path=$($s.path)"
}
$lines | Set-Content -Path $outTxtAbs -Encoding UTF8

Write-Host "manifest_json=$outManifestAbs"
Write-Host "manifest_txt=$outTxtAbs"
