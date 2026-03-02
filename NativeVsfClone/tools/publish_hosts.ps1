param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "[publish_hosts] $Message"
}

function Assert-Command {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        throw "Required command not found: $Name"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build"
$reportDir = Join-Path $buildDir "reports"
$distRoot = Join-Path $repoRoot "dist"
$wpfDist = Join-Path $distRoot "wpf"
$winUiDist = Join-Path $distRoot "winui"
$nativeCoreDll = Join-Path $buildDir "Release\nativecore.dll"
$logPath = Join-Path $reportDir "host_publish_latest.txt"

New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
New-Item -ItemType Directory -Force -Path $distRoot | Out-Null

$logLines = [System.Collections.Generic.List[string]]::new()
$logLines.Add("Host publish run: $(Get-Date -Format o)")
$logLines.Add("Configuration: $Configuration")
$logLines.Add("RuntimeIdentifier: $RuntimeIdentifier")

Assert-Command "cmake"
Assert-Command "dotnet"

if (-not $SkipNativeBuild) {
    Write-Step "Building native Release artifacts..."
    cmake --build $buildDir --config Release | Out-Host
    $logLines.Add("Native build: executed")
} else {
    $logLines.Add("Native build: skipped")
}

if (-not (Test-Path $nativeCoreDll)) {
    throw "nativecore.dll not found at expected path: $nativeCoreDll"
}

$wpfProject = Join-Path $repoRoot "host\WpfHost\WpfHost.csproj"
$winUiProject = Join-Path $repoRoot "host\WinUiHost\WinUiHost.csproj"

Write-Step "Publishing WPF host..."
dotnet publish $wpfProject -c $Configuration -r $RuntimeIdentifier --self-contained true /p:PublishSingleFile=true /p:PublishTrimmed=false | Out-Host

Write-Step "Publishing WinUI host..."
dotnet publish $winUiProject -c $Configuration -r $RuntimeIdentifier --self-contained true /p:PublishSingleFile=true /p:PublishTrimmed=false /p:WindowsAppSDKSelfContained=true | Out-Host

$wpfPublishDir = Join-Path $repoRoot "host\WpfHost\bin\$Configuration\net8.0-windows\$RuntimeIdentifier\publish"
$winUiPublishDir = Join-Path $repoRoot "host\WinUiHost\bin\$Configuration\net8.0-windows10.0.19041.0\$RuntimeIdentifier\publish"

if (-not (Test-Path $wpfPublishDir)) {
    throw "WPF publish output not found: $wpfPublishDir"
}
if (-not (Test-Path $winUiPublishDir)) {
    throw "WinUI publish output not found: $winUiPublishDir"
}

Write-Step "Preparing dist output folders..."
if (Test-Path $wpfDist) { Remove-Item -Recurse -Force $wpfDist }
if (Test-Path $winUiDist) { Remove-Item -Recurse -Force $winUiDist }
New-Item -ItemType Directory -Force -Path $wpfDist | Out-Null
New-Item -ItemType Directory -Force -Path $winUiDist | Out-Null

Copy-Item -Path (Join-Path $wpfPublishDir "*") -Destination $wpfDist -Recurse -Force
Copy-Item -Path (Join-Path $winUiPublishDir "*") -Destination $winUiDist -Recurse -Force

Copy-Item -Path $nativeCoreDll -Destination $wpfDist -Force
Copy-Item -Path $nativeCoreDll -Destination $winUiDist -Force

$wpfExe = Get-ChildItem -Path $wpfDist -Filter "*.exe" -File | Select-Object -First 1
$winUiExe = Get-ChildItem -Path $winUiDist -Filter "*.exe" -File | Select-Object -First 1

$logLines.Add("WPF dist: $wpfDist")
$logLines.Add("WPF exe: $($wpfExe.FullName)")
$logLines.Add("WinUI dist: $winUiDist")
$logLines.Add("WinUI exe: $($winUiExe.FullName)")
$logLines.Add("NativeCore copy: $nativeCoreDll -> dist/{wpf,winui}")

$logLines | Set-Content -Path $logPath -Encoding UTF8

Write-Step "Done."
Write-Step "WPF dist: $wpfDist"
Write-Step "WinUI dist: $winUiDist"
Write-Step "Report: $logPath"
