param(
    [string]$OutputJson = ".\build\reports\winui_env_fingerprint.json",
    [string]$OutputTxt = ".\build\reports\winui_env_fingerprint.txt",
    [switch]$RequireKnownGoodContract
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$Path, [string]$BaseDirectory)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Try-Exec {
    param([string]$FilePath, [string[]]$Args = @())
    try {
        & $FilePath @Args 2>$null
    } catch {
        return @()
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$resolvedJson = Resolve-AbsolutePath -Path $OutputJson -BaseDirectory $repoRoot
$resolvedTxt = Resolve-AbsolutePath -Path $OutputTxt -BaseDirectory $repoRoot
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedJson) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resolvedTxt) | Out-Null

$dotnetSdks = @(Try-Exec -FilePath "dotnet" -Args @("--list-sdks"))
$dotnetRuntimes = @(Try-Exec -FilePath "dotnet" -Args @("--list-runtimes"))
$nugetSources = @(Try-Exec -FilePath "dotnet" -Args @("nuget", "list", "source"))
$vsWherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vswhereRaw = if (Test-Path -LiteralPath $vsWherePath) {
    @(Try-Exec -FilePath $vsWherePath -Args @("-all", "-format", "json"))
} else {
    @()
}

$windowsSdkRoot = "C:\Program Files (x86)\Windows Kits\10\Include"
$sdkVersions = @()
if (Test-Path -LiteralPath $windowsSdkRoot) {
    $sdkVersions = @(Get-ChildItem -Path $windowsSdkRoot -Directory | Select-Object -ExpandProperty Name | Sort-Object)
}

$knownGoodSdk = "10.0.19041.0"
$hasKnownGoodSdk = $sdkVersions -contains $knownGoodSdk
$hasDotnet8 = ($dotnetSdks | Where-Object { $_ -match "^8\." }).Count -gt 0
$nugetEnabledLines = @($nugetSources | Where-Object { $_ -match "Enabled:\s*True" })

$status = if ($hasKnownGoodSdk -and $hasDotnet8 -and $nugetEnabledLines.Count -gt 0) { "READY" } else { "NOT_READY" }
$laneFingerprint = "{0}|sdk:{1}|dotnet8:{2}|nugetEnabled:{3}" -f $env:COMPUTERNAME, ($sdkVersions -join ","), $hasDotnet8, $nugetEnabledLines.Count
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $laneHash = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($laneFingerprint))
} finally {
    $sha.Dispose()
}
$laneId = (($laneHash | ForEach-Object { $_.ToString("x2") }) -join "").Substring(0, 16)

$obj = [ordered]@{
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    machine = $env:COMPUTERNAME
    user = $env:USERNAME
    os_caption = ""
    os_version = ""
    lane_id = $laneId
    lane_fingerprint = $laneFingerprint
    known_good_contract = [ordered]@{
        windows_sdk_required = $knownGoodSdk
        windows_sdk_present = [bool]$hasKnownGoodSdk
        dotnet8_present = [bool]$hasDotnet8
        nuget_enabled_source_count = [int]$nugetEnabledLines.Count
    }
    status = $status
    dotnet_sdks = $dotnetSdks
    dotnet_runtimes = $dotnetRuntimes
    nuget_sources = $nugetSources
    windows_sdk_versions = $sdkVersions
    vswhere_raw = $vswhereRaw
}

try {
    $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
    $obj.os_caption = [string]$os.Caption
    $obj.os_version = [string]$os.Version
} catch {
    $obj.os_caption = [System.Environment]::OSVersion.VersionString
    $obj.os_version = [string][System.Environment]::OSVersion.Version
}
$obj | ConvertTo-Json -Depth 8 | Set-Content -Path $resolvedJson -Encoding UTF8

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("WinUI Environment Fingerprint")
$lines.Add("GeneratedUtc: $($obj.generated_utc)")
$lines.Add("Machine: $($obj.machine)")
$lines.Add("LaneId: $laneId")
$lines.Add("Status: $status")
$lines.Add("WindowsSdkRequired: $knownGoodSdk")
$lines.Add("WindowsSdkPresent: $hasKnownGoodSdk")
$lines.Add("Dotnet8Present: $hasDotnet8")
$lines.Add("NuGetEnabledSourceCount: $($nugetEnabledLines.Count)")
$lines.Add("OutputJson: $resolvedJson")
$lines | Set-Content -Path $resolvedTxt -Encoding UTF8

Write-Host "json=$resolvedJson"
Write-Host "txt=$resolvedTxt"

if ($RequireKnownGoodContract -and $status -ne "READY") {
    exit 1
}
