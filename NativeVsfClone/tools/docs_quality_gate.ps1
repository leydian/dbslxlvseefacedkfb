param(
    [switch]$FailOnAbsolutePathLinks = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$docsRoot = Join-Path $repoRoot 'docs'
$indexPath = Join-Path $docsRoot 'INDEX.md'
$reportsDir = Join-Path $docsRoot 'reports'
$weeklyRoot = Join-Path $reportsDir 'weekly'
$weeklyIndexPath = Join-Path $weeklyRoot 'INDEX.md'
$domainIndexPath = Join-Path $reportsDir 'DOMAIN_INDEX.md'
$legacyMapPath = Join-Path $reportsDir 'legacy-map.md'

foreach ($required in @($indexPath, $reportsDir, $weeklyRoot, $weeklyIndexPath, $domainIndexPath, $legacyMapPath)) {
    if (-not (Test-Path $required)) {
        Write-Error "Missing required documentation path: $required"
    }
}

function Test-Utf8File {
    param([Parameter(Mandatory)] [string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $utf8 = [System.Text.UTF8Encoding]::new($false, $true)
    try {
        [void]$utf8.GetString($bytes)
        return $true
    }
    catch {
        return $false
    }
}

function Get-MarkdownLinks {
    param([Parameter(Mandatory)] [string]$Path)

    $raw = Get-Content -Path $Path -Raw -Encoding UTF8
    return [regex]::Matches($raw, '\[[^\]]+\]\(([^)]+)\)') | ForEach-Object { $_.Groups[1].Value.Trim() }
}

$indexLinks = @(Get-MarkdownLinks -Path $indexPath)
$absolutePathLinks = @()
$brokenIndexLinks = @()
foreach ($target in $indexLinks) {
    if ([string]::IsNullOrWhiteSpace($target)) { continue }
    if ($target -match '^(https?|mailto):' -or $target.StartsWith('#')) { continue }

    if ($target -match '^/[A-Za-z]:/' -or $target -match '^[A-Za-z]:[\\/]') {
        $absolutePathLinks += $target
    }

    $cleanTarget = ($target -split '#')[0]
    if ([string]::IsNullOrWhiteSpace($cleanTarget)) { continue }
    $resolved = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $indexPath) $cleanTarget))
    if (-not (Test-Path $resolved)) {
        $brokenIndexLinks += $target
    }
}

$weeklyDirs = @(Get-ChildItem -Path $weeklyRoot -Directory)
$weeklyMissingIndex = @()
$weeklyMissingSummary = @()
$weeklyEmpty = @()
$invalidCanonicalNames = @()

foreach ($w in $weeklyDirs) {
    $wIndex = Join-Path $w.FullName 'INDEX.md'
    $wSummary = Join-Path $w.FullName 'SUMMARY.md'

    if (-not (Test-Path $wIndex)) { $weeklyMissingIndex += $w.Name }
    if (-not (Test-Path $wSummary)) { $weeklyMissingSummary += $w.Name }

    $reports = @(Get-ChildItem -Path $w.FullName -File -Filter '*.md' | Where-Object { $_.Name -ne 'INDEX.md' -and $_.Name -ne 'SUMMARY.md' })
    if ($reports.Count -eq 0) {
        $weeklyEmpty += $w.Name
    }

    foreach ($r in $reports) {
        if ($r.Name -notmatch '^\d{4}-\d{2}-\d{2}_[a-z0-9]+_[a-z0-9_]+(?:_v\d+)?\.md$') {
            $invalidCanonicalNames += $r.FullName
        }
    }
}

$legacyMapRaw = Get-Content -Path $legacyMapPath -Raw -Encoding UTF8
$mapMatches = [regex]::Matches($legacyMapRaw, '\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|\s*`([^`]+)`\s*\|')
$mapOldSet = New-Object 'System.Collections.Generic.HashSet[string]'
$mapNewSet = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($m in $mapMatches) {
    [void]$mapOldSet.Add($m.Groups[1].Value.Trim())
    [void]$mapNewSet.Add($m.Groups[2].Value.Trim())
}

$stubFiles = @(Get-ChildItem -Path $reportsDir -File -Filter '*.md' | Where-Object {
    $_.Name -ne 'TEMPLATE.md' -and $_.Name -ne 'DOMAIN_INDEX.md' -and $_.Name -ne 'legacy-map.md'
})

$legacyMissingMap = @()
$legacyBrokenTarget = @()
$legacyInvalidName = @()

foreach ($stub in $stubFiles) {
    if ($stub.Name -notmatch '^.+_\d{4}-\d{2}-\d{2}\.md$') {
        $legacyInvalidName += $stub.FullName
        continue
    }

    $oldRel = 'docs/reports/{0}' -f $stub.Name
    if (-not $mapOldSet.Contains($oldRel)) {
        $legacyMissingMap += $oldRel
    }

    $stubRaw = Get-Content -Path $stub.FullName -Raw -Encoding UTF8
    $linkMatch = [regex]::Match($stubRaw, '\-\s*New location:\s*\[[^\]]+\]\(([^)]+)\)')
    if (-not $linkMatch.Success) {
        $legacyBrokenTarget += $oldRel
        continue
    }

    $target = $linkMatch.Groups[1].Value.Trim()
    $resolved = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $stub.FullName) $target))
    if (-not (Test-Path $resolved)) {
        $legacyBrokenTarget += $oldRel
    }
}

$mapMissingNewTarget = @()
foreach ($newRel in $mapNewSet) {
    $newPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $newRel))
    if (-not (Test-Path $newPath)) {
        $mapMissingNewTarget += $newRel
    }
}

$utf8Targets = @(
    (Join-Path $repoRoot 'README.md'),
    (Join-Path $repoRoot 'CHANGELOG.md'),
    $indexPath,
    (Join-Path $docsRoot 'CONTRIBUTING_DOCS.md'),
    (Join-Path $repoRoot 'build/reports/README.md')
)
$utf8Targets += @(Get-ChildItem -Path $docsRoot -Recurse -File -Filter '*.md' | ForEach-Object { $_.FullName })
$utf8Targets = $utf8Targets | Sort-Object -Unique

$utf8Invalid = @()
foreach ($f in $utf8Targets) {
    if (-not (Test-Path $f)) { continue }
    if (-not (Test-Utf8File -Path $f)) {
        $utf8Invalid += $f
    }
}

Write-Host '=== Docs Quality Gate ==='
Write-Host ("Index links: {0}" -f $indexLinks.Count)
Write-Host ("Weekly folders: {0}" -f $weeklyDirs.Count)
Write-Host ("Legacy stubs: {0}" -f $stubFiles.Count)
Write-Host ("Legacy map entries: {0}" -f $mapOldSet.Count)
Write-Host ("Broken links in docs/INDEX.md: {0}" -f $brokenIndexLinks.Count)
Write-Host ("Missing weekly INDEX.md: {0}" -f $weeklyMissingIndex.Count)
Write-Host ("Missing weekly SUMMARY.md: {0}" -f $weeklyMissingSummary.Count)
Write-Host ("Empty weekly folders: {0}" -f $weeklyEmpty.Count)
Write-Host ("Invalid canonical names: {0}" -f $invalidCanonicalNames.Count)
Write-Host ("Legacy stubs missing map: {0}" -f $legacyMissingMap.Count)
Write-Host ("Legacy stubs with invalid target: {0}" -f $legacyBrokenTarget.Count)
Write-Host ("Legacy map missing new target: {0}" -f $mapMissingNewTarget.Count)
Write-Host ("UTF-8 invalid files: {0}" -f $utf8Invalid.Count)

if ($brokenIndexLinks.Count -gt 0) {
    Write-Host ''
    Write-Host 'Broken links in docs/INDEX.md:'
    $brokenIndexLinks | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($weeklyMissingIndex.Count -gt 0) {
    Write-Host ''
    Write-Host 'Weekly folders missing INDEX.md:'
    $weeklyMissingIndex | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($weeklyMissingSummary.Count -gt 0) {
    Write-Host ''
    Write-Host 'Weekly folders missing SUMMARY.md:'
    $weeklyMissingSummary | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($weeklyEmpty.Count -gt 0) {
    Write-Host ''
    Write-Host 'Empty weekly folders:'
    $weeklyEmpty | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($invalidCanonicalNames.Count -gt 0) {
    Write-Host ''
    Write-Host 'Invalid canonical report names:'
    $invalidCanonicalNames | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f (Resolve-Path -Relative $_)) }
}

if ($legacyInvalidName.Count -gt 0) {
    Write-Host ''
    Write-Host 'Invalid legacy stub names:'
    $legacyInvalidName | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f (Resolve-Path -Relative $_)) }
}

if ($legacyMissingMap.Count -gt 0) {
    Write-Host ''
    Write-Host 'Legacy stubs missing from legacy-map:'
    $legacyMissingMap | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($legacyBrokenTarget.Count -gt 0) {
    Write-Host ''
    Write-Host 'Legacy stubs with missing new target:'
    $legacyBrokenTarget | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($mapMissingNewTarget.Count -gt 0) {
    Write-Host ''
    Write-Host 'Legacy map rows with missing target file:'
    $mapMissingNewTarget | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($FailOnAbsolutePathLinks -and $absolutePathLinks.Count -gt 0) {
    Write-Host ''
    Write-Host 'Absolute path links are not allowed:'
    $absolutePathLinks | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
}

if ($utf8Invalid.Count -gt 0) {
    Write-Host ''
    Write-Host 'Files with invalid UTF-8:'
    $utf8Invalid | ForEach-Object { Write-Host ("- {0}" -f (Resolve-Path -Relative $_)) }
}

$failed = ($brokenIndexLinks.Count -gt 0) -or
          ($weeklyMissingIndex.Count -gt 0) -or
          ($weeklyMissingSummary.Count -gt 0) -or
          ($weeklyEmpty.Count -gt 0) -or
          ($invalidCanonicalNames.Count -gt 0) -or
          ($legacyInvalidName.Count -gt 0) -or
          ($legacyMissingMap.Count -gt 0) -or
          ($legacyBrokenTarget.Count -gt 0) -or
          ($mapMissingNewTarget.Count -gt 0) -or
          ($utf8Invalid.Count -gt 0)

if ($FailOnAbsolutePathLinks -and $absolutePathLinks.Count -gt 0) {
    $failed = $true
}

if ($failed) {
    Write-Error 'Docs quality gate failed.'
}

Write-Host ''
Write-Host 'Docs quality gate passed.'
