param(
    [switch]$FailOnAbsolutePathLinks = $true
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$docsRoot = Join-Path $repoRoot 'docs'
$indexPath = Join-Path $docsRoot 'INDEX.md'
$reportsDir = Join-Path $docsRoot 'reports'

if (-not (Test-Path $indexPath)) {
    Write-Error "Missing docs index: $indexPath"
}
if (-not (Test-Path $reportsDir)) {
    Write-Error "Missing reports dir: $reportsDir"
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

$indexRaw = Get-Content -Path $indexPath -Raw -Encoding UTF8
$linkMatches = [regex]::Matches($indexRaw, '\[[^\]]+\]\(([^)]+)\)')

$linkTargets = @()
foreach ($m in $linkMatches) {
    $linkTargets += $m.Groups[1].Value.Trim()
}

$absolutePathLinks = @()
$brokenLinks = @()
$resolvedLinks = @()

foreach ($target in $linkTargets) {
    if ([string]::IsNullOrWhiteSpace($target)) {
        continue
    }
    if ($target -match '^(https?|mailto):' -or $target.StartsWith('#')) {
        continue
    }

    if ($target -match '^/[A-Za-z]:/' -or $target -match '^[A-Za-z]:[\\/]') {
        $absolutePathLinks += $target
    }

    $cleanTarget = ($target -split '#')[0]
    if ([string]::IsNullOrWhiteSpace($cleanTarget)) {
        continue
    }

    $resolved = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $indexPath) $cleanTarget))
    $resolvedLinks += $resolved

    if (-not (Test-Path $resolved)) {
        $brokenLinks += $target
    }
}

$reportFiles = @(Get-ChildItem -Path $reportsDir -File -Filter '*.md' | ForEach-Object {
    [System.IO.Path]::GetFullPath($_.FullName)
})

$linkedReportFiles = @($resolvedLinks | Where-Object { $_ -like "*$([IO.Path]::DirectorySeparatorChar)docs$([IO.Path]::DirectorySeparatorChar)reports$([IO.Path]::DirectorySeparatorChar)*.md" } | Sort-Object -Unique)
$missingReports = @($reportFiles | Where-Object { $_ -notin $linkedReportFiles })

$utf8Targets = @(
    (Join-Path $repoRoot 'README.md'),
    (Join-Path $repoRoot 'CHANGELOG.md'),
    (Join-Path $docsRoot 'INDEX.md'),
    (Join-Path $docsRoot 'CONTRIBUTING_DOCS.md'),
    (Join-Path $repoRoot 'build\reports\README.md')
)
$utf8Targets += Get-ChildItem -Path $reportsDir -File -Filter '*.md' | ForEach-Object { $_.FullName }
$utf8Targets = $utf8Targets | Sort-Object -Unique

$utf8Invalid = @()
foreach ($f in $utf8Targets) {
    if (-not (Test-Path $f)) {
        continue
    }
    if (-not (Test-Utf8File -Path $f)) {
        $utf8Invalid += $f
    }
}

Write-Host '=== Docs Quality Gate ==='
Write-Host ("Index links: {0}" -f $linkTargets.Count)
Write-Host ("Report files: {0}" -f $reportFiles.Count)
Write-Host ("Linked report files: {0}" -f $linkedReportFiles.Count)
Write-Host ("Missing report links: {0}" -f @($missingReports).Count)
Write-Host ("Broken links: {0}" -f @($brokenLinks).Count)
Write-Host ("UTF-8 invalid files: {0}" -f $utf8Invalid.Count)

if (@($missingReports).Count -gt 0) {
    Write-Host ''
    Write-Host 'Missing reports in docs/INDEX.md:'
    $missingReports | ForEach-Object { Write-Host ("- {0}" -f (Resolve-Path -Relative $_)) }
}

if (@($brokenLinks).Count -gt 0) {
    Write-Host ''
    Write-Host 'Broken links in docs/INDEX.md:'
    $brokenLinks | Sort-Object -Unique | ForEach-Object { Write-Host ("- {0}" -f $_) }
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

$failed = (@($missingReports).Count -gt 0) -or (@($brokenLinks).Count -gt 0) -or ($utf8Invalid.Count -gt 0)
if ($FailOnAbsolutePathLinks -and $absolutePathLinks.Count -gt 0) {
    $failed = $true
}

if ($failed) {
    Write-Error 'Docs quality gate failed.'
}

Write-Host ''
Write-Host 'Docs quality gate passed.'
