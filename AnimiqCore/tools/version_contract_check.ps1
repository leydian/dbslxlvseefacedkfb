param(
    [string]$HostCoreProject = ".\host\HostCore\HostCore.csproj",
    [string]$UnityPackageJson = ".\unity\Packages\com.animiq.miq\package.json",
    [string]$OutputTxt = ".\build\reports\version_contract_check.txt"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $HostCoreProject)) { throw "HostCore csproj not found: $HostCoreProject" }
if (-not (Test-Path $UnityPackageJson)) { throw "Unity package.json not found: $UnityPackageJson" }

$csproj = Get-Content -Path $HostCoreProject -Raw
$tfmLine = Select-String -InputObject $csproj -Pattern "<TargetFramework>.*</TargetFramework>" | Select-Object -First 1
$tfm = if ($null -ne $tfmLine) { ($tfmLine.Matches[0].Value -replace "</?TargetFramework>", "") } else { "unknown" }

$pkg = Get-Content -Path $UnityPackageJson -Raw | ConvertFrom-Json
$pkgVersion = if ($null -ne $pkg.version) { "$($pkg.version)" } else { "unknown" }

$status = "PASS"
$reasons = @()
if (-not $tfm.StartsWith("net8.0")) {
    $status = "FAIL"
    $reasons += "HostCore target framework is not net8.0*"
}
if ($pkgVersion -eq "unknown") {
    $status = "FAIL"
    $reasons += "Unity package version missing"
}

$lines = @()
$lines += "Version Contract Check"
$lines += "GeneratedUTC: $((Get-Date).ToUniversalTime().ToString('s'))"
$lines += "Status: $status"
$lines += "HostCore TargetFramework: $tfm"
$lines += "Unity Package Version: $pkgVersion"
if ($reasons.Count -gt 0) {
    $lines += "Reasons:"
    foreach ($r in $reasons) { $lines += "- $r" }
}

$outDir = Split-Path -Parent $OutputTxt
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}
$lines | Set-Content -Path $OutputTxt

if ($status -ne "PASS") {
    exit 1
}
