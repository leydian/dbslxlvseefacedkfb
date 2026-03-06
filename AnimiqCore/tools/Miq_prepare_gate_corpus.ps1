param(
    [string]$SourceDir = ".",
    [string]$OutputDir = ".\build\gate_corpus\miq",
    [int]$MinSampleCount = 10,
    [switch]$IncludeBuildArtifacts,
    [string]$ManifestOutPath = ".\build\gate_corpus\miq\sample_manifest.json"
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedSource = Resolve-AbsolutePath -Path $SourceDir -BaseDirectory $repoRoot
$resolvedOutput = Resolve-AbsolutePath -Path $OutputDir -BaseDirectory $repoRoot
$resolvedManifest = Resolve-AbsolutePath -Path $ManifestOutPath -BaseDirectory $repoRoot

if (-not (Test-Path -LiteralPath $resolvedSource)) {
    throw "SourceDir not found: $resolvedSource"
}

New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedManifest) | Out-Null

$all = Get-ChildItem -Path $resolvedSource -Recurse -File -Filter *.miq -ErrorAction SilentlyContinue
$selected = [System.Collections.Generic.List[object]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($f in $all) {
    $full = $f.FullName
    if ($full -match '\\NativeAnimiq\\NativeAnimiq\\') {
        continue
    }
    if ($full.StartsWith($resolvedOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
        continue
    }
    if (-not $IncludeBuildArtifacts) {
        if ($full -match '\\build\\') {
            continue
        }
    }
    if ($seen.Contains($full)) { continue }
    $seen.Add($full) | Out-Null
    $selected.Add($f)
}

if ($selected.Count -lt $MinSampleCount) {
    foreach ($f in $all) {
        $full = $f.FullName
        if ($full -match '\\NativeAnimiq\\NativeAnimiq\\') {
            continue
        }
        if ($full.StartsWith($resolvedOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
            continue
        }
        if ($seen.Contains($full)) { continue }
        $seen.Add($full) | Out-Null
        $selected.Add($f)
        if ($selected.Count -ge $MinSampleCount) { break }
    }
}

if ($selected.Count -lt $MinSampleCount) {
    throw "Not enough .miq samples found. required=$MinSampleCount found=$($selected.Count)"
}

$ordered = @($selected | Sort-Object -Property LastWriteTime -Descending | Select-Object -First $MinSampleCount)
$manifestRows = [System.Collections.Generic.List[object]]::new()
$index = 0
foreach ($f in $ordered) {
    $index++
    $ext = [System.IO.Path]::GetExtension($f.Name)
    $base = [System.IO.Path]::GetFileNameWithoutExtension($f.Name)
    $safeBase = ($base -replace '[^A-Za-z0-9._-]', '_')
    $destName = ("sample_{0:D2}_{1}{2}" -f $index, $safeBase, $ext)
    Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $resolvedOutput $destName) -Force
    $sampleClass = if ($index -le 6) { "normal" } elseif ($index -le 8) { "boundary" } else { "corrupt" }
    $manifestRows.Add([ordered]@{
        name = $destName
        sample_class = $sampleClass
        expected_primary_error = "NONE"
        expected_max_warning_codes = 0
        source_path = $f.FullName
    })
}

$manifest = [ordered]@{
    generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
    source_dir = $resolvedSource
    output_dir = $resolvedOutput
    min_sample_count = [int]$MinSampleCount
    include_build_artifacts = [bool]$IncludeBuildArtifacts
    samples = @($manifestRows)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Path $resolvedManifest -Encoding UTF8

$summaryPath = Join-Path $resolvedOutput "prepare_summary.txt"
$lines = @()
$lines += "MIQ Gate Corpus Prepared"
$lines += "Generated: $(Get-Date -Format o)"
$lines += "SourceDir: $resolvedSource"
$lines += "OutputDir: $resolvedOutput"
$lines += "SampleCount: $($ordered.Count)"
$lines += "Manifest: $resolvedManifest"
$lines += "IncludeBuildArtifacts: $IncludeBuildArtifacts"
$lines += ""
$lines += "Samples:"
foreach ($m in $manifestRows) {
    $lines += "- $($m.name) [$($m.sample_class)] <- $($m.source_path)"
}
$lines | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "summary=$summaryPath"
Write-Host "manifest=$resolvedManifest"
