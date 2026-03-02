param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$IncludeWinUi
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "[publish_hosts] $Message"
}

function Assert-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Stop-IfRunning {
    param([string]$ProcessName)
    try {
        Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    } catch {
        # ignore
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build"
$buildHotfixDir = Join-Path $repoRoot "build_hotfix"
$reportDir = Join-Path $buildDir "reports"
$distRoot = Join-Path $repoRoot "dist"
$wpfDist = Join-Path $distRoot "wpf"
$winUiDist = Join-Path $distRoot "winui"
$nativeCoreDll = Join-Path $buildDir "Release\nativecore.dll"
$nativeCoreDllHotfix = Join-Path $buildHotfixDir "Release\nativecore.dll"
$logPath = Join-Path $reportDir "host_publish_latest.txt"

New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
New-Item -ItemType Directory -Force -Path $distRoot | Out-Null

$log = [System.Collections.Generic.List[string]]::new()
$log.Add("Host publish run: $(Get-Date -Format o)")
$log.Add("Configuration: $Configuration")
$log.Add("RuntimeIdentifier: $RuntimeIdentifier")
$log.Add("IncludeWinUi: $IncludeWinUi")

Assert-Command "cmake"
Assert-Command "dotnet"

Stop-IfRunning "WpfHost"
Stop-IfRunning "WinUiHost"

if (-not $SkipNativeBuild) {
    Write-Step "Building nativecore..."
    $nativeBuildSucceeded = $false
    try {
        cmake --build $buildDir --config Release --target nativecore | Out-Host
        $nativeBuildSucceeded = $true
        $log.Add("Native build: build/nativecore success")
    } catch {
        $log.Add("Native build: build/nativecore failed, trying build_hotfix")
    }

    if (-not $nativeBuildSucceeded) {
        Write-Step "Falling back to build_hotfix for locked-dll cases..."
        cmake -S $repoRoot -B $buildHotfixDir -G "Visual Studio 17 2022" -A x64 | Out-Host
        cmake --build $buildHotfixDir --config Release --target nativecore | Out-Host
        if (-not (Test-Path $nativeCoreDllHotfix)) {
            throw "nativecore.dll not found in fallback build output: $nativeCoreDllHotfix"
        }
        Copy-Item -Path $nativeCoreDllHotfix -Destination $nativeCoreDll -Force
        $log.Add("Native build: fallback build_hotfix used")
    }
} else {
    $log.Add("Native build: skipped")
}

if (-not (Test-Path $nativeCoreDll)) {
    throw "nativecore.dll not found at expected path: $nativeCoreDll"
}

$wpfProject = Join-Path $repoRoot "host\WpfHost\WpfHost.csproj"
$winUiProject = Join-Path $repoRoot "host\WinUiHost\WinUiHost.csproj"

Write-Step "Publishing WPF host to dist/wpf..."
dotnet publish $wpfProject `
    -c $Configuration `
    -r $RuntimeIdentifier `
    --self-contained true `
    /p:PublishSingleFile=true `
    /p:PublishTrimmed=false `
    -o $wpfDist | Out-Host

if (-not (Test-Path (Join-Path $wpfDist "WpfHost.exe"))) {
    throw "WPF publish output not found: $wpfDist"
}

Copy-Item -Path $nativeCoreDll -Destination $wpfDist -Force
$log.Add("WPF dist: $wpfDist")
$log.Add("WPF exe: $(Join-Path $wpfDist 'WpfHost.exe')")

if ($IncludeWinUi) {
    Write-Step "Publishing WinUI host to dist/winui..."
    dotnet publish $winUiProject `
        -c $Configuration `
        -r $RuntimeIdentifier `
        --self-contained true `
        -p:Platform=x64 `
        /p:PublishSingleFile=false `
        /p:PublishTrimmed=false `
        /p:WindowsAppSDKSelfContained=true `
        -o $winUiDist | Out-Host

    if (-not (Test-Path (Join-Path $winUiDist "WinUiHost.exe"))) {
        throw "WinUI publish output not found: $winUiDist"
    }
    Copy-Item -Path $nativeCoreDll -Destination $winUiDist -Force
    $log.Add("WinUI dist: $winUiDist")
    $log.Add("WinUI exe: $(Join-Path $winUiDist 'WinUiHost.exe')")
} else {
    $log.Add("WinUI publish: skipped (use -IncludeWinUi)")
}

$log.Add("NativeCore copy: $nativeCoreDll")
$log | Set-Content -Path $logPath -Encoding UTF8

Write-Step "Done."
Write-Step "WPF EXE: $(Join-Path $wpfDist 'WpfHost.exe')"
if ($IncludeWinUi) {
    Write-Step "WinUI EXE: $(Join-Path $winUiDist 'WinUiHost.exe')"
}
Write-Step "Report: $logPath"
