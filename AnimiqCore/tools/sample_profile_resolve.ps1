param(
    [string]$SampleDir = "..\sample",
    [ValidateSet("fixed_set", "real_large_set")][string]$Profile = "fixed_set",
    [string]$ProfilesDir = ".\tools\sample_profiles",
    [string]$OutputPath = ".\build\reports\sample_profile_resolve_latest.txt"
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
$resolvedSampleDir = Resolve-AbsolutePath -Path $SampleDir -BaseDirectory $repoRoot
$resolvedProfilesDir = Resolve-AbsolutePath -Path $ProfilesDir -BaseDirectory $repoRoot
$resolvedOutput = Resolve-AbsolutePath -Path $OutputPath -BaseDirectory $repoRoot

if (-not (Test-Path $resolvedSampleDir)) { throw "sample dir not found: $resolvedSampleDir" }
$profilePath = Join-Path $resolvedProfilesDir "$Profile.txt"
if (-not (Test-Path $profilePath)) { throw "profile not found: $profilePath" }

$patterns = Get-Content -Path $profilePath | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
$matches = [System.Collections.Generic.List[string]]::new()
foreach ($p in $patterns) {
    $trimmed = $p.Trim()
    Get-ChildItem -Path $resolvedSampleDir -File -Filter $trimmed -ErrorAction SilentlyContinue | ForEach-Object {
        $matches.Add($_.Name)
    }
}

$unique = @($matches | Sort-Object -Unique)
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Sample Profile Resolve")
$lines.Add("Generated: $(Get-Date -Format o)")
$lines.Add("Profile: $Profile")
$lines.Add("SampleDir: $resolvedSampleDir")
$lines.Add("PatternCount: $($patterns.Count)")
$lines.Add("ResolvedCount: $($unique.Count)")
$lines.Add("")
$lines.Add("Patterns:")
foreach ($p in $patterns) { $lines.Add("- $($p.Trim())") }
$lines.Add("")
$lines.Add("Resolved:")
foreach ($m in $unique) { $lines.Add("- $m") }

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedOutput) | Out-Null
$lines | Set-Content -Path $resolvedOutput -Encoding UTF8
Write-Host "output=$resolvedOutput"
