param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$IncludeWinUi,
    [switch]$NoRestore,
    [bool]$CollectWinUiDiagnostics = $true,
    [bool]$CollectManagedXamlDiagnostics = $true,
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
$log.Add("CollectManagedXamlDiagnostics: $CollectManagedXamlDiagnostics")
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
        [string]$FailureClass,
        [string]$FailureClassConfidence,
        [string]$DiagCommand,
        [int]$DiagExitCode,
        [string]$BinlogPath,
        [string]$DiagLogPath,
        [string]$DiagStderrPath,
        [string]$ObjDumpPath,
        [hashtable]$Preflight,
        [hashtable]$EnvironmentSummary,
        [string]$ManagedDiagCommand,
        [int]$ManagedDiagExitCode,
        [string]$ManagedDiagLogPath,
        [string]$ManagedDiagStderrPath,
        [string[]]$RootCauseHints,
        [hashtable]$PreflightProbe
    )

    $legacyPreflight = $Preflight
    if ($null -ne $Preflight -and $Preflight.Contains("probe")) {
        $legacyPreflight = [ordered]@{
            passed = $Preflight.passed
            failed_checks = $Preflight.failed_checks
            detected_sdks = $Preflight.detected_sdks
            recommended_actions = $Preflight.recommended_actions
        }
    }

    $manifest = [ordered]@{
        generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
        reason = $Reason
        publish_error = $PublishError
        failure_class = $FailureClass
        failure_class_confidence = $FailureClassConfidence
        diagnostics_command = $DiagCommand
        diagnostics_exit_code = $DiagExitCode
        artifacts = [ordered]@{
            binlog = $BinlogPath
            diag_log = $DiagLogPath
            stderr_log = $DiagStderrPath
            obj_dump_dir = $ObjDumpPath
            managed_diag_log = $ManagedDiagLogPath
            managed_stderr_log = $ManagedDiagStderrPath
        }
        managed_diagnostics_command = $ManagedDiagCommand
        managed_diagnostics_exit_code = $ManagedDiagExitCode
        root_cause_hints = $RootCauseHints
        preflight = $legacyPreflight
        preflight_probe = $PreflightProbe
        environment = $EnvironmentSummary
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $ManifestPath -Encoding UTF8
}

function Get-WinUiEnvironmentSummary {
    $summary = [ordered]@{
        os_version = [System.Environment]::OSVersion.VersionString
        dotnet_sdks = @()
        dotnet_runtimes = @()
        visual_studio = @()
    }

    try {
        $summary.dotnet_sdks = (& dotnet --list-sdks 2>$null)
    } catch {
        $summary.dotnet_sdks = @("unavailable")
    }

    try {
        $summary.dotnet_runtimes = (& dotnet --list-runtimes 2>$null)
    } catch {
        $summary.dotnet_runtimes = @("unavailable")
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        try {
            $summary.visual_studio = @(& $vswhere -all -products * -format json | ConvertFrom-Json | ForEach-Object {
                "$($_.displayName) $($_.catalog.productDisplayVersion)"
            })
        } catch {
            $summary.visual_studio = @("vswhere parse failed")
        }
    } else {
        $summary.visual_studio = @("vswhere not found")
    }

    return $summary
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
        $_.Name -ieq "output.json" -or $_.Name -ieq "input.json" -or $_.Extension -in @(".log", ".err", ".wrn")
    }

    foreach ($file in $candidates) {
        $relative = $file.FullName.Substring($ObjRoot.Length).TrimStart('\')
        $dest = Join-Path $ObjDumpRoot $relative
        $destDir = Split-Path -Parent $dest
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
        Copy-Item -Path $file.FullName -Destination $dest -Force
    }
}

function Get-WinUiRootCauseHints {
    param(
        [string]$DiagLogPath,
        [string]$ManagedDiagLogPath,
        [hashtable]$EnvironmentSummary
    )

    $hints = [System.Collections.Generic.List[string]]::new()

    if (Test-Path $DiagLogPath) {
        if (Select-String -Path $DiagLogPath -Pattern "XamlCompiler.exe" -SimpleMatch -Quiet) {
            $hints.Add("XamlCompiler.exe exit path observed (MSB3073).")
        }
        if (Select-String -Path $DiagLogPath -Pattern "NU1101" -SimpleMatch -Quiet) {
            $hints.Add("NuGet package resolution failed (NU1101). Verify package source connectivity.")
        }
        if (Select-String -Path $DiagLogPath -Pattern "NU1301" -SimpleMatch -Quiet) {
            $hints.Add("NuGet source access failed (NU1301). Verify network/proxy and source config.")
        }
    }

    if ((Test-Path $ManagedDiagLogPath) -and (Select-String -Path $ManagedDiagLogPath -Pattern "System.Security.Permissions" -SimpleMatch -Quiet)) {
        $hints.Add("Managed XAML compiler task load failed: missing System.Security.Permissions assembly.")
    }
    if ((Test-Path $ManagedDiagLogPath) -and (Select-String -Path $ManagedDiagLogPath -Pattern "WMC9999" -SimpleMatch -Quiet)) {
        $hints.Add("Managed XAML compiler reported WMC9999 (platform unsupported/internal operation not supported).")
    }
    if ($null -ne $EnvironmentSummary -and $null -ne $EnvironmentSummary.dotnet_sdks) {
        $sdkEntries = @($EnvironmentSummary.dotnet_sdks)
        $hasNet8Sdk = $false
        foreach ($sdk in $sdkEntries) {
            $text = "$sdk"
            if ($text.StartsWith("8.")) {
                $hasNet8Sdk = $true
                break
            }
        }
        if (-not $hasNet8Sdk) {
            $hints.Add(".NET 8 SDK was not detected in dotnet --list-sdks; WinUI net8.0 build may fail under SDK-only 9.x environments.")
        }
    }

    if ($hints.Count -eq 0) {
        $hints.Add("No explicit root-cause hint extracted from diagnostics logs.")
    }

    return $hints.ToArray()
}

function Test-WinUiToolchainPreconditions {
    param(
        [hashtable]$EnvironmentSummary
    )

    $failedChecks = [System.Collections.Generic.List[string]]::new()
    $recommendedActions = [System.Collections.Generic.List[string]]::new()
    $detectedSdks = @()

    if ($null -ne $EnvironmentSummary -and $null -ne $EnvironmentSummary.dotnet_sdks) {
        $detectedSdks = @($EnvironmentSummary.dotnet_sdks)
    }

    $hasNet8Sdk = $false
    foreach ($sdk in $detectedSdks) {
        if ("$sdk".StartsWith("8.")) {
            $hasNet8Sdk = $true
            break
        }
    }
    if (-not $hasNet8Sdk) {
        $failedChecks.Add("MISSING_DOTNET_8_SDK")
        $recommendedActions.Add("Install .NET 8 SDK (8.x) and re-run publish.")
    }

    $hasVisualStudio = $false
    if ($null -ne $EnvironmentSummary -and $null -ne $EnvironmentSummary.visual_studio) {
        $vsEntries = @($EnvironmentSummary.visual_studio)
        foreach ($entry in $vsEntries) {
            if (-not [string]::IsNullOrWhiteSpace("$entry") -and "$entry" -notmatch "not found" -and "$entry" -notmatch "parse failed") {
                $hasVisualStudio = $true
                break
            }
        }
    }
    if (-not $hasVisualStudio) {
        $failedChecks.Add("MISSING_VISUAL_STUDIO_DISCOVERY")
        $recommendedActions.Add("Verify Visual Studio Build Tools 2022 with Windows app build components is installed.")
    }

    $windowsSdkProbePaths = @(
        "C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.19041.0\Facade\Windows.winmd",
        "C:\Program Files (x86)\Windows Kits\10\References\10.0.19041.0\Windows.Foundation.FoundationContract\3.0.0.0\Windows.Foundation.FoundationContract.winmd"
    )
    $foundWindowsSdkMetadataPath = ""
    foreach ($probePath in $windowsSdkProbePaths) {
        if (Test-Path $probePath) {
            $foundWindowsSdkMetadataPath = $probePath
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($foundWindowsSdkMetadataPath)) {
        $failedChecks.Add("MISSING_WINDOWS_SDK_19041_METADATA")
        $recommendedActions.Add("Install Windows 10 SDK 10.0.19041.0 metadata/facade components for WinUI net8 targeting.")
    }

    return [ordered]@{
        passed = ($failedChecks.Count -eq 0)
        failed_checks = $failedChecks.ToArray()
        detected_sdks = $detectedSdks
        recommended_actions = $recommendedActions.ToArray()
        probe = [ordered]@{
            checks = @(
                [ordered]@{
                    check = "DOTNET_8_SDK"
                    detected = $hasNet8Sdk
                    evidence = $detectedSdks
                },
                [ordered]@{
                    check = "VISUAL_STUDIO_DISCOVERY"
                    detected = $hasVisualStudio
                    evidence = if ($null -ne $EnvironmentSummary -and $null -ne $EnvironmentSummary.visual_studio) { @($EnvironmentSummary.visual_studio) } else { @() }
                },
                [ordered]@{
                    check = "WINDOWS_SDK_19041_METADATA"
                    detected = -not [string]::IsNullOrWhiteSpace($foundWindowsSdkMetadataPath)
                    checked_paths = $windowsSdkProbePaths
                    detected_path = $foundWindowsSdkMetadataPath
                }
            )
        }
    }
}

function Get-WinUiFailureClass {
    param(
        [string]$DiagLogPath,
        [string]$ManagedDiagLogPath,
        [hashtable]$Preflight
    )

    if ($null -ne $Preflight -and $null -ne $Preflight.passed -and -not [bool]$Preflight.passed) {
        $checks = @($Preflight.failed_checks)
        if ($checks -contains "MISSING_DOTNET_8_SDK") {
            return [ordered]@{ Class = "TOOLCHAIN_MISSING_DOTNET8"; Confidence = "high" }
        }
        return [ordered]@{ Class = "TOOLCHAIN_PRECONDITION_FAILED"; Confidence = "high" }
    }

    # Classification priority:
    # 1) toolchain preconditions (handled above)
    # 2) managed WMC9999 platform unsupported
    # 3) diagnostics-specific execution/path failures (NuGet/XamlCompiler/etc.)
    # 4) unknown
    if (Test-Path $ManagedDiagLogPath) {
        if (Select-String -Path $ManagedDiagLogPath -Pattern "WMC9999" -SimpleMatch -Quiet) {
            return [ordered]@{ Class = "TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED"; Confidence = "high" }
        }
        if (Select-String -Path $ManagedDiagLogPath -Pattern "System.Security.Permissions" -SimpleMatch -Quiet) {
            return [ordered]@{ Class = "MANAGED_XAML_TASK_MISSING_DEP"; Confidence = "high" }
        }
    }

    if (Test-Path $DiagLogPath) {
        if ((Select-String -Path $DiagLogPath -Pattern "NU1101" -SimpleMatch -Quiet) -or
            (Select-String -Path $DiagLogPath -Pattern "NU1301" -SimpleMatch -Quiet)) {
            return [ordered]@{ Class = "NUGET_SOURCE_UNREACHABLE"; Confidence = "high" }
        }
        if ((Select-String -Path $DiagLogPath -Pattern "MSB3073" -SimpleMatch -Quiet) -and
            (Select-String -Path $DiagLogPath -Pattern "XamlCompiler.exe" -SimpleMatch -Quiet)) {
            return [ordered]@{ Class = "XAML_COMPILER_EXEC_FAIL"; Confidence = "high" }
        }
    }

    return [ordered]@{ Class = "UNKNOWN"; Confidence = "low" }
}

function Collect-WinUiDiagnostics {
    param(
        [string]$Reason,
        [string]$PublishError,
        [hashtable]$Preflight
    )

    New-Item -ItemType Directory -Force -Path $resolvedWinUiDiagDir | Out-Null

    $binlogPath = Join-Path $resolvedWinUiDiagDir "winui_build.binlog"
    $diagLogPath = Join-Path $resolvedWinUiDiagDir "winui_build_diag.log"
    $diagStderrPath = Join-Path $resolvedWinUiDiagDir "winui_build_stderr.log"
    $managedDiagLogPath = Join-Path $resolvedWinUiDiagDir "winui_build_managed_diag.log"
    $managedDiagStderrPath = Join-Path $resolvedWinUiDiagDir "winui_build_managed_stderr.log"
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
    $envSummary = Get-WinUiEnvironmentSummary

    & dotnet @diagArgs 1> $diagLogPath 2> $diagStderrPath
    $diagExitCode = $LASTEXITCODE

    $managedDiagCommandText = ""
    $managedDiagExitCode = -1
    if ($CollectManagedXamlDiagnostics) {
        $managedDiagArgs = @(
            "build",
            $winUiProject,
            "-c", $Configuration,
            "-p:Platform=x64",
            "-v:m",
            "-p:UseXamlCompilerExecutable=false"
        )
        if ($NoRestore) {
            $managedDiagArgs += "--no-restore"
        }
        $managedDiagCommandText = "dotnet " + ($managedDiagArgs -join " ")
        & dotnet @managedDiagArgs 1> $managedDiagLogPath 2> $managedDiagStderrPath
        $managedDiagExitCode = $LASTEXITCODE
    }

    Copy-WinUiObjDiagnostics -ObjRoot $objRoot -ObjDumpRoot $objDumpPath
    $rootCauseHints = Get-WinUiRootCauseHints -DiagLogPath $diagLogPath -ManagedDiagLogPath $managedDiagLogPath -EnvironmentSummary $envSummary
    $failure = Get-WinUiFailureClass -DiagLogPath $diagLogPath -ManagedDiagLogPath $managedDiagLogPath -Preflight $Preflight
    Write-WinUiDiagnosticManifest `
        -ManifestPath $manifestPath `
        -Reason $Reason `
        -PublishError $PublishError `
        -FailureClass $failure.Class `
        -FailureClassConfidence $failure.Confidence `
        -DiagCommand $diagCommandText `
        -DiagExitCode $diagExitCode `
        -BinlogPath $binlogPath `
        -DiagLogPath $diagLogPath `
        -DiagStderrPath $diagStderrPath `
        -ObjDumpPath $objDumpPath `
        -Preflight $Preflight `
        -EnvironmentSummary $envSummary `
        -ManagedDiagCommand $managedDiagCommandText `
        -ManagedDiagExitCode $managedDiagExitCode `
        -ManagedDiagLogPath $managedDiagLogPath `
        -ManagedDiagStderrPath $managedDiagStderrPath `
        -RootCauseHints $rootCauseHints `
        -PreflightProbe $Preflight.probe

    $log.Add("WinUI diagnostics command: $diagCommandText")
    $log.Add("WinUI diagnostics exit code: $diagExitCode")
    if ($CollectManagedXamlDiagnostics) {
        $log.Add("WinUI managed diagnostics command: $managedDiagCommandText")
        $log.Add("WinUI managed diagnostics exit code: $managedDiagExitCode")
        $log.Add("WinUI managed diagnostics log: $managedDiagLogPath")
        $log.Add("WinUI managed diagnostics stderr log: $managedDiagStderrPath")
    } else {
        $log.Add("WinUI managed diagnostics: skipped (CollectManagedXamlDiagnostics=false)")
    }
    foreach ($hint in $rootCauseHints) {
        $log.Add("WinUI root-cause hint: $hint")
    }
    $log.Add("WinUI failure class: $($failure.Class)")
    $log.Add("WinUI failure class confidence: $($failure.Confidence)")
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
    $envSummaryForPreflight = Get-WinUiEnvironmentSummary
    $winUiPreflight = Test-WinUiToolchainPreconditions -EnvironmentSummary $envSummaryForPreflight
    if (-not [bool]$winUiPreflight.passed) {
        $preflightError = "WinUI toolchain preflight failed: $($winUiPreflight.failed_checks -join ', ')"
        $log.Add("WinUI preflight: FAIL")
        foreach ($check in $winUiPreflight.failed_checks) {
            $log.Add("WinUI preflight failed check: $check")
        }
        foreach ($action in $winUiPreflight.recommended_actions) {
            $log.Add("WinUI preflight recommended action: $action")
        }

        if ($CollectWinUiDiagnostics) {
            Write-Step "WinUI preflight failed; collecting diagnostics..."
            Collect-WinUiDiagnostics -Reason "winui preflight failed" -PublishError $preflightError -Preflight $winUiPreflight
            Write-Step "WinUI diagnostics saved under: $resolvedWinUiDiagDir"
        } else {
            $log.Add("WinUI diagnostics: skipped (CollectWinUiDiagnostics=false)")
        }

        $log | Set-Content -Path $logPath -Encoding UTF8
        throw $preflightError
    }

    $log.Add("WinUI preflight: PASS")
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
            Collect-WinUiDiagnostics -Reason "winui publish failed" -PublishError $publishErrorText -Preflight $winUiPreflight
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
