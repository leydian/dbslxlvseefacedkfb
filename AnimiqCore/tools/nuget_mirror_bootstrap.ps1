param(
    [string]$MirrorSourceName = "animiq-local-mirror",
    [string]$MirrorSourcePath = ".\build\nuget-mirror",
    [switch]$EnableOnlyMirror,
    [switch]$DisableNuGetOrg,
    [string]$SummaryPath = ".\build\reports\nuget_mirror_bootstrap_summary.txt"
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
$resolvedMirrorPath = Resolve-AbsolutePath -Path $MirrorSourcePath -BaseDirectory $repoRoot
$resolvedSummary = Resolve-AbsolutePath -Path $SummaryPath -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path $resolvedMirrorPath | Out-Null

$existing = @(& dotnet nuget list source --format short 2>$null)
$hasMirror = $false
foreach ($line in $existing) {
    $text = "$line".Trim()
    if ($text -match '^[A-Za-z]+\s+(.+)$') {
        if ($Matches[1].Trim() -ieq $resolvedMirrorPath) {
            $hasMirror = $true
            break
        }
    }
}

if (-not $hasMirror) {
    & dotnet nuget add source $resolvedMirrorPath -n $MirrorSourceName | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to add mirror source: $resolvedMirrorPath"
    }
}

if ($DisableNuGetOrg) {
    & dotnet nuget disable source nuget.org | Out-Host
}

if ($EnableOnlyMirror) {
    $sources = @(& dotnet nuget list source --format short 2>$null)
    foreach ($line in $sources) {
        $text = "$line".Trim()
        if ($text -match '^([A-Za-z]+)\s+(.+)$') {
            $pathOrUrl = $Matches[2].Trim()
            if ($pathOrUrl -ieq $resolvedMirrorPath) {
                & dotnet nuget enable source $MirrorSourceName | Out-Null
            } elseif ($pathOrUrl -match '^https?://') {
                # best effort: disable by known names
                if ($pathOrUrl -match 'nuget\.org') {
                    & dotnet nuget disable source nuget.org | Out-Null
                }
            }
        }
    }
}

$finalSources = @(& dotnet nuget list source 2>$null)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("NuGet Mirror Bootstrap Summary")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("MirrorSourceName: $MirrorSourceName")
$lines.Add("MirrorSourcePath: $resolvedMirrorPath")
$lines.Add("EnableOnlyMirror: $EnableOnlyMirror")
$lines.Add("DisableNuGetOrg: $DisableNuGetOrg")
$lines.Add("")
$lines.Add("Sources:")
foreach ($line in $finalSources) {
    $lines.Add("$line")
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedSummary) | Out-Null
$lines | Set-Content -Path $resolvedSummary -Encoding UTF8
Write-Host "summary=$resolvedSummary"
