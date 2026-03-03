param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$IncludeWinUi,
    [switch]$NoRestore,
    [bool]$CollectWinUiDiagnostics = $true,
    [string]$WinUiDiagDir = ".\build\reports\winui"
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

function Invoke-DotNetCommand {
    param(
        [Parameter(Mandatory = $true)][string[]]$Args,
        [Parameter(Mandatory = $true)][string]$Description
    )

    & dotnet @Args | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE (dotnet $($Args -join ' '))"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build"
$buildHotfixDir = Join-Path $repoRoot "build_hotfix"
$reportDir = Join-Path $buildDir "reports"
$resolvedWinUiDiagDir = if ([System.IO.Path]::IsPathRooted($WinUiDiagDir)) {
    [System.IO.Path]::GetFullPath($WinUiDiagDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $WinUiDiagDir))
}
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
$log.Add("NoRestore: $NoRestore")
$log.Add("CollectWinUiDiagnostics: $CollectWinUiDiagnostics")
$log.Add("WinUiDiagDir: $resolvedWinUiDiagDir")

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

function Write-WinUiDiagnosticManifest {
    param(
        [string]$ManifestPath,
        [string]$Reason,
        [string]$PublishError,
        [string]$DiagCommand,
        [int]$DiagExitCode,
        [string]$BinlogPath,
        [string]$DiagLogPath,
        [string]$DiagStderrPath,
        [string]$ObjDumpPath
    )

    $manifest = [ordered]@{
        generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
        reason = $Reason
        publish_error = $PublishError
        diagnostics_command = $DiagCommand
        diagnostics_exit_code = $DiagExitCode
        artifacts = [ordered]@{
            binlog = $BinlogPath
            diag_log = $DiagLogPath
            stderr_log = $DiagStderrPath
            obj_dump_dir = $ObjDumpPath
        }
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $ManifestPath -Encoding UTF8
}

function Copy-WinUiObjDiagnostics {
    param(
        [string]$ObjRoot,
        [string]$ObjDumpRoot
    )

    if (-not (Test-Path $ObjRoot)) {
        return
    }

    $candidates = Get-ChildItem -Path $ObjRoot -Recurse -File | Where-Object {
        $_.Name -ieq "output.json" -or $_.Extension -in @(".log", ".err", ".wrn")
    }

    foreach ($file in $candidates) {
        $relative = $file.FullName.Substring($ObjRoot.Length).TrimStart('\')
        $dest = Join-Path $ObjDumpRoot $relative
        $destDir = Split-Path -Parent $dest
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
        Copy-Item -Path $file.FullName -Destination $dest -Force
    }
}

function Collect-WinUiDiagnostics {
    param(
        [string]$Reason,
        [string]$PublishError
    )

    New-Item -ItemType Directory -Force -Path $resolvedWinUiDiagDir | Out-Null

    $binlogPath = Join-Path $resolvedWinUiDiagDir "winui_build.binlog"
    $diagLogPath = Join-Path $resolvedWinUiDiagDir "winui_build_diag.log"
    $diagStderrPath = Join-Path $resolvedWinUiDiagDir "winui_build_stderr.log"
    $manifestPath = Join-Path $resolvedWinUiDiagDir "winui_diagnostic_manifest.json"
    $objRoot = Join-Path $repoRoot "host\WinUiHost\obj"
    $objDumpPath = Join-Path $resolvedWinUiDiagDir "obj-dump"

    New-Item -ItemType Directory -Force -Path $objDumpPath | Out-Null

    $diagArgs = @(
        "build",
        $winUiProject,
        "-c", $Configuration,
        "-p:Platform=x64",
        "-v:diag",
        "-bl:$binlogPath"
    )
    if ($NoRestore) {
        $diagArgs += "--no-restore"
    }
    $diagCommandText = "dotnet " + ($diagArgs -join " ")

    & dotnet @diagArgs 1> $diagLogPath 2> $diagStderrPath
    $diagExitCode = $LASTEXITCODE

    Copy-WinUiObjDiagnostics -ObjRoot $objRoot -ObjDumpRoot $objDumpPath
    Write-WinUiDiagnosticManifest `
        -ManifestPath $manifestPath `
        -Reason $Reason `
        -PublishError $PublishError `
        -DiagCommand $diagCommandText `
        -DiagExitCode $diagExitCode `
        -BinlogPath $binlogPath `
        -DiagLogPath $diagLogPath `
        -DiagStderrPath $diagStderrPath `
        -ObjDumpPath $objDumpPath

    $log.Add("WinUI diagnostics command: $diagCommandText")
    $log.Add("WinUI diagnostics exit code: $diagExitCode")
    $log.Add("WinUI diagnostics manifest: $manifestPath")
    $log.Add("WinUI diagnostics binlog: $binlogPath")
    $log.Add("WinUI diagnostics diag log: $diagLogPath")
    $log.Add("WinUI diagnostics stderr log: $diagStderrPath")
    $log.Add("WinUI diagnostics obj dump: $objDumpPath")
}

$wpfPublishFailed = $false
$wpfPublishErrorText = ""
try {
    Write-Step "Publishing WPF host to dist/wpf..."
    $wpfPublishArgs = @(
        "publish",
        $wpfProject,
        "-c", $Configuration,
        "-r", $RuntimeIdentifier,
        "--self-contained", "true",
        "/p:PublishSingleFile=true",
        "/p:PublishTrimmed=false",
        "-o", $wpfDist
    )
    if ($NoRestore) {
        $wpfPublishArgs += "--no-restore"
    }
    Invoke-DotNetCommand -Description "WPF publish" -Args $wpfPublishArgs

    if (-not (Test-Path (Join-Path $wpfDist "WpfHost.exe"))) {
        throw "WPF publish output not found: $wpfDist"
    }

    Copy-Item -Path $nativeCoreDll -Destination $wpfDist -Force
    $log.Add("WPF dist: $wpfDist")
    $log.Add("WPF exe: $(Join-Path $wpfDist 'WpfHost.exe')")
} catch {
    $wpfPublishFailed = $true
    $wpfPublishErrorText = ($_ | Out-String).Trim()
    $log.Add("WPF publish: failed")
    $log.Add("WPF publish error: $wpfPublishErrorText")
    if ($IncludeWinUi) {
        Write-Step "WPF publish failed; continuing to WinUI publish for additional diagnostics."
    } else {
        $log | Set-Content -Path $logPath -Encoding UTF8
        throw
    }
}

if ($IncludeWinUi) {
    try {
        Write-Step "Publishing WinUI host to dist/winui..."
        $winUiPublishArgs = @(
            "publish",
            $winUiProject,
            "-c", $Configuration,
            "-r", $RuntimeIdentifier,
            "--self-contained", "true",
            "-p:Platform=x64",
            "/p:PublishSingleFile=false",
            "/p:PublishTrimmed=false",
            "/p:WindowsAppSDKSelfContained=true",
            "-o", $winUiDist
        )
        if ($NoRestore) {
            $winUiPublishArgs += "--no-restore"
        }
        Invoke-DotNetCommand -Description "WinUI publish" -Args $winUiPublishArgs

        if (-not (Test-Path (Join-Path $winUiDist "WinUiHost.exe"))) {
            throw "WinUI publish output not found: $winUiDist"
        }
        Copy-Item -Path $nativeCoreDll -Destination $winUiDist -Force
        $log.Add("WinUI dist: $winUiDist")
        $log.Add("WinUI exe: $(Join-Path $winUiDist 'WinUiHost.exe')")
    } catch {
        $publishErrorText = $_ | Out-String
        $log.Add("WinUI publish: failed")
        $log.Add("WinUI publish error: $($publishErrorText.Trim())")

        if ($CollectWinUiDiagnostics) {
            Write-Step "WinUI publish failed; collecting diagnostics..."
            Collect-WinUiDiagnostics -Reason "winui publish failed" -PublishError $publishErrorText
            Write-Step "WinUI diagnostics saved under: $resolvedWinUiDiagDir"
        } else {
            $log.Add("WinUI diagnostics: skipped (CollectWinUiDiagnostics=false)")
        }

        $log | Set-Content -Path $logPath -Encoding UTF8
        throw
    }
} else {
    $log.Add("WinUI publish: skipped (use -IncludeWinUi)")
}

$log.Add("NativeCore copy: $nativeCoreDll")
$log | Set-Content -Path $logPath -Encoding UTF8

if ($wpfPublishFailed) {
    throw "WPF publish failed: $wpfPublishErrorText"
}

$log | Set-Content -Path $logPath -Encoding UTF8

Write-Step "Done."
Write-Step "WPF EXE: $(Join-Path $wpfDist 'WpfHost.exe')"
if ($IncludeWinUi) {
    Write-Step "WinUI EXE: $(Join-Path $winUiDist 'WinUiHost.exe')"
}
Write-Step "Report: $logPath"
