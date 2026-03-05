param(
    [string]$Configuration = "Release",
    [string]$RuntimeIdentifier = "win-x64",
    [switch]$SkipNativeBuild,
    [switch]$IncludeWinUi,
    [switch]$NoRestore,
    [bool]$CollectWinUiDiagnostics = $true,
    [bool]$CollectManagedXamlDiagnostics = $true,
    [string]$WinUiDiagDir = ".\build\reports\winui",
    [ValidateSet("full", "diag-only")][string]$WinUiDiagnosticsProfile = "full",
    [bool]$RunWpfLaunchSmoke = $true,
    [bool]$WpfLaunchSmokeFailOnError = $false,
    [int]$WpfLaunchSmokeDurationSeconds = 6,
    [string]$WpfLaunchSmokeReportPath = ".\build\reports\wpf_launch_smoke_latest.txt",
    [int]$WinUiRestoreRetryCount = 1,
    [int]$NuGetProbeTimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "[publish_hosts] $Message"
}

function Copy-SpoutRuntimeBinaries {
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [Parameter(Mandatory = $true)][string]$DistDir,
        [Parameter(Mandatory = $true)][System.Collections.Generic.List[string]]$Log
    )

    $spoutBinDir = Join-Path $RepoRoot "third_party\Spout2\bin"
    if (-not (Test-Path $spoutBinDir)) {
        $Log.Add("Spout runtime copy: skipped (bin dir not found: $spoutBinDir)")
        return
    }

    $dlls = Get-ChildItem -Path $spoutBinDir -File -Filter "*.dll" -ErrorAction SilentlyContinue
    if ($null -eq $dlls -or $dlls.Count -eq 0) {
        $Log.Add("Spout runtime copy: skipped (no dll files in $spoutBinDir)")
        return
    }

    foreach ($dll in $dlls) {
        Copy-Item -Path $dll.FullName -Destination (Join-Path $DistDir $dll.Name) -Force
    }
    $Log.Add("Spout runtime copy: $($dlls.Count) dll(s) copied from $spoutBinDir to $DistDir")
}

function Assert-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Invoke-CMakeCommand {
    param(
        [Parameter(Mandatory = $true)][string[]]$Args,
        [Parameter(Mandatory = $true)][string]$Description
    )

    Push-Location $repoRoot
    try {
        & cmake @Args | Out-Host
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        throw "$Description failed with exit code $exitCode (cmake $($Args -join ' '))"
    }
}

function Assert-NativeCoreCopyIntegrity {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][System.Collections.Generic.List[string]]$Log
    )

    if (-not (Test-Path $SourcePath)) {
        throw "$Label nativecore source missing: $SourcePath"
    }
    if (-not (Test-Path $DestinationPath)) {
        throw "$Label nativecore destination missing: $DestinationPath"
    }

    $src = Get-Item $SourcePath
    $dst = Get-Item $DestinationPath
    $srcHash = (Get-FileHash -Path $SourcePath -Algorithm SHA256).Hash
    $dstHash = (Get-FileHash -Path $DestinationPath -Algorithm SHA256).Hash

    $Log.Add("$Label nativecore source: $SourcePath")
    $Log.Add("$Label nativecore destination: $DestinationPath")
    $Log.Add("$Label nativecore source timestamp: $($src.LastWriteTime.ToString('o'))")
    $Log.Add("$Label nativecore destination timestamp: $($dst.LastWriteTime.ToString('o'))")
    $Log.Add("$Label nativecore source hash: $srcHash")
    $Log.Add("$Label nativecore destination hash: $dstHash")

    if ($srcHash -ne $dstHash) {
        throw "$Label nativecore integrity mismatch: source and destination hashes differ."
    }
}

function Get-DotNetVersionInfo {
    param([string]$WorkingDirectory)

    $resolved = if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) { $PWD.Path } else { $WorkingDirectory }
    Push-Location $resolved
    try {
        $versionText = (& dotnet --version 2>$null | Select-Object -First 1).Trim()
    } finally {
        Pop-Location
    }
    $major = 0
    if ($versionText -match '^(\d+)\.') {
        $major = [int]$Matches[1]
    }
    return [ordered]@{
        version = $versionText
        major = $major
        working_directory = $resolved
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

    Push-Location $repoRoot
    try {
        & dotnet @Args | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "$Description failed with exit code $LASTEXITCODE (dotnet $($Args -join ' '))"
        }
    } finally {
        Pop-Location
    }
}

function Invoke-DotNetCommandWithRetry {
    param(
        [Parameter(Mandatory = $true)][string[]]$Args,
        [Parameter(Mandatory = $true)][string]$Description,
        [int]$RetryCount = 0,
        [int]$RetryDelaySeconds = 3
    )

    $attempt = 0
    while ($true) {
        $attempt++
        Push-Location $repoRoot
        try {
            $output = & dotnet @Args 2>&1
            $output | Out-Host
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        if ($exitCode -eq 0) {
            return
        }

        $outputText = ($output | ForEach-Object { "$_" }) -join "`n"
        $nugetTransient = $outputText -match "NU1301"
        if ($attempt -le $RetryCount -and $nugetTransient) {
            Write-Step "$Description failed with NU1301; retrying ($attempt/$RetryCount) after ${RetryDelaySeconds}s..."
            Start-Sleep -Seconds $RetryDelaySeconds
            continue
        }

        throw "$Description failed with exit code $exitCode (dotnet $($Args -join ' '))"
    }
}

function Get-NuGetSourceProbe {
    param([int]$TimeoutSeconds = 8)

    $probe = [ordered]@{
        generated_at_utc = (Get-Date).ToUniversalTime().ToString("o")
        timeout_seconds = $TimeoutSeconds
        proxy = [ordered]@{
            http_proxy = $env:HTTP_PROXY
            https_proxy = $env:HTTPS_PROXY
            no_proxy = $env:NO_PROXY
        }
        sources = @()
        summary = [ordered]@{
            total = 0
            enabled = 0
            reachable = 0
            unreachable = 0
            unknown = 0
        }
    }

    $sources = [System.Collections.Generic.List[object]]::new()
    $shortSourceLines = @()
    try {
        $shortSourceLines = @(& dotnet nuget list source --format short 2>$null)
    } catch {
        $shortSourceLines = @()
    }

    foreach ($line in $shortSourceLines) {
        $text = "$line".Trim()
        if ([string]::IsNullOrWhiteSpace($text)) {
            continue
        }
        # Locale-agnostic short format examples:
        # E https://api.nuget.org/v3/index.json
        # EM C:\Program Files (x86)\Microsoft SDKs\NuGetPackages\
        if ($text -match '^([A-Za-z]+)\s+(.+)$') {
            $flags = $Matches[1].ToUpperInvariant()
            $urlOrPath = $Matches[2].Trim()
            $isEnabled = $flags.Contains("E")
            $sources.Add([ordered]@{
                name = if ($urlOrPath.StartsWith("http", [System.StringComparison]::OrdinalIgnoreCase)) { $urlOrPath } else { "local-feed" }
                enabled = $isEnabled
                url = $urlOrPath
                reachable = $null
                status = "unknown"
                error = ""
            })
        }
    }

    if ($sources.Count -eq 0) {
        # Fallback for environments where short-format parsing fails.
        $sourceLines = @()
        try {
            $sourceLines = @(& dotnet nuget list source 2>$null)
        } catch {
            $sourceLines = @()
        }

        foreach ($line in $sourceLines) {
            $text = "$line".Trim()
            if ([string]::IsNullOrWhiteSpace($text)) {
                continue
            }
            if ($text -match '^\s*\d+\.\s+(.+?)\s+\[(.+?)\]\s*$') {
                $stateText = $Matches[2].ToLowerInvariant()
                $isEnabled = $stateText.Contains("enabled") -or $stateText.Contains("사용")
                $sources.Add([ordered]@{
                    name = $Matches[1].Trim()
                    enabled = $isEnabled
                    url = ""
                    reachable = $null
                    status = "unknown"
                    error = ""
                })
                continue
            }
            if ($text -match '^\s*(https?://\S+)\s*$') {
                if ($sources.Count -gt 0) {
                    $entry = $sources[$sources.Count - 1]
                    $entry.url = $Matches[1]
                }
            }
        }
    }

    foreach ($source in $sources) {
        if (-not [bool]$source.enabled) {
            $source.status = "disabled"
            continue
        }
        if ([string]::IsNullOrWhiteSpace("$($source.url)")) {
            $source.status = "enabled_no_url"
            continue
        }
        if (-not ("$($source.url)" -match '^https?://')) {
            $source.status = "enabled_local_source"
            continue
        }
        try {
            $null = Invoke-WebRequest -Uri $source.url -Method Head -TimeoutSec $TimeoutSeconds -UseBasicParsing
            $source.reachable = $true
            $source.status = "reachable"
        } catch {
            $source.reachable = $false
            $source.status = "unreachable"
            $source.error = "$($_.Exception.Message)"
        }
    }

    $probe.sources = $sources.ToArray()
    $probe.summary.total = $probe.sources.Count
    $probe.summary.enabled = @($probe.sources | Where-Object { [bool]$_.enabled }).Count
    $probe.summary.reachable = @($probe.sources | Where-Object { [bool]$_.enabled -and $_.status -eq "reachable" }).Count
    $probe.summary.unreachable = @($probe.sources | Where-Object { [bool]$_.enabled -and $_.status -eq "unreachable" }).Count
    $probe.summary.unknown = @($probe.sources | Where-Object { [bool]$_.enabled -and $_.status -notin @("reachable", "unreachable") }).Count

    return $probe
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
$resolvedWpfLaunchSmokeReportPath = if ([System.IO.Path]::IsPathRooted($WpfLaunchSmokeReportPath)) {
    [System.IO.Path]::GetFullPath($WpfLaunchSmokeReportPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $WpfLaunchSmokeReportPath))
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
$hostPublishMode = if ($IncludeWinUi) { "WPF_PLUS_WINUI" } else { "WPF_ONLY" }
$log.Add("Host publish run: $(Get-Date -Format o)")
$log.Add("HostPublishMode: $hostPublishMode")
$log.Add("Configuration: $Configuration")
$log.Add("RuntimeIdentifier: $RuntimeIdentifier")
$log.Add("IncludeWinUi: $IncludeWinUi")
$log.Add("NoRestore: $NoRestore")
$log.Add("CollectWinUiDiagnostics: $CollectWinUiDiagnostics")
$log.Add("CollectManagedXamlDiagnostics: $CollectManagedXamlDiagnostics")
$log.Add("WinUiDiagnosticsProfile: $WinUiDiagnosticsProfile")
$dotnetContract = Get-DotNetVersionInfo -WorkingDirectory $repoRoot
$log.Add("dotnet version (repo root): $($dotnetContract.version)")
if ($IncludeWinUi -and $dotnetContract.major -ne 8) {
    $log.Add("WinUI SDK contract: FAIL (expected .NET SDK major 8)")
    $log | Set-Content -Path $logPath -Encoding UTF8
    throw "WinUI SDK contract failed. Expected .NET SDK major 8 at repo root ($repoRoot), actual=$($dotnetContract.version)"
}
$log.Add("WinUiDiagDir: $resolvedWinUiDiagDir")
$log.Add("RunWpfLaunchSmoke: $RunWpfLaunchSmoke")
$log.Add("WpfLaunchSmokeFailOnError: $WpfLaunchSmokeFailOnError")
$log.Add("WpfLaunchSmokeDurationSeconds: $WpfLaunchSmokeDurationSeconds")
$log.Add("WpfLaunchSmokeReportPath: $resolvedWpfLaunchSmokeReportPath")
$log.Add("WinUiRestoreRetryCount: $WinUiRestoreRetryCount")
$log.Add("NuGetProbeTimeoutSeconds: $NuGetProbeTimeoutSeconds")

Assert-Command "cmake"
Assert-Command "dotnet"

Stop-IfRunning "WpfHost"
Stop-IfRunning "WinUiHost"

if (-not $SkipNativeBuild) {
    Write-Step "Building nativecore..."
    $nativeBuildSucceeded = $false
    try {
        Invoke-CMakeCommand -Description "native build (build dir)" -Args @("--build", $buildDir, "--config", "Release", "--target", "nativecore")
        $nativeBuildSucceeded = $true
        $log.Add("Native build: build/nativecore success")
    } catch {
        $log.Add("Native build: build/nativecore failed, trying build_hotfix")
    }

    if (-not $nativeBuildSucceeded) {
        Write-Step "Falling back to build_hotfix for locked-dll cases..."
        Invoke-CMakeCommand -Description "native configure (build_hotfix)" -Args @("-S", $repoRoot, "-B", $buildHotfixDir, "-G", "Visual Studio 17 2022", "-A", "x64")
        Invoke-CMakeCommand -Description "native build (build_hotfix)" -Args @("--build", $buildHotfixDir, "--config", "Release", "--target", "nativecore")
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
$wpfLaunchSmokeScript = Join-Path $repoRoot "tools\wpf_launch_smoke.ps1"

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
        [hashtable]$PreflightProbe,
        [object[]]$Profiles,
        [hashtable]$NuGetProbe,
        [string]$DotNetVersion,
        [hashtable]$SdkResolutionContext
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
        profiles = $Profiles
        preflight = $legacyPreflight
        preflight_probe = $PreflightProbe
        nuget_probe = $NuGetProbe
        dotnet_version = $DotNetVersion
        sdk_resolution_context = $SdkResolutionContext
        environment = $EnvironmentSummary
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $ManifestPath -Encoding UTF8
}

function Get-WinUiProfileHints {
    param(
        [string]$ProfileName,
        [string]$DiagLogPath,
        [string]$ManagedDiagLogPath
    )

    $hints = [System.Collections.Generic.List[string]]::new()

    if ($ProfileName -eq "diag-default") {
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
    }

    if ($ProfileName -eq "managed-xaml") {
        if ((Test-Path $ManagedDiagLogPath) -and (Select-String -Path $ManagedDiagLogPath -Pattern "System.Security.Permissions" -SimpleMatch -Quiet)) {
            $hints.Add("Managed XAML compiler task load failed: missing System.Security.Permissions assembly.")
        }
        if ((Test-Path $ManagedDiagLogPath) -and (Select-String -Path $ManagedDiagLogPath -Pattern "WMC9999" -SimpleMatch -Quiet)) {
            $hints.Add("Managed XAML compiler reported WMC9999 (platform unsupported/internal operation not supported).")
        }
    }

    if ($hints.Count -eq 0) {
        $hints.Add("No explicit profile-local root-cause hint extracted.")
    }
    return $hints.ToArray()
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
        [hashtable]$EnvironmentSummary,
        [hashtable]$NuGetProbe
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
        if ((Select-String -Path $DiagLogPath -Pattern "401" -SimpleMatch -Quiet) -or
            (Select-String -Path $DiagLogPath -Pattern "403" -SimpleMatch -Quiet)) {
            $hints.Add("NuGet feed returned authorization failure (401/403). Check credential provider/session token.")
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
    if ($null -ne $NuGetProbe -and $null -ne $NuGetProbe.summary) {
        if ([int]$NuGetProbe.summary.enabled -gt 0 -and [int]$NuGetProbe.summary.reachable -eq 0) {
            $hints.Add("NuGet source probe found no reachable enabled sources; verify feed URL/proxy/firewall.")
        }
    }

    if ($hints.Count -eq 0) {
        $hints.Add("No explicit root-cause hint extracted from diagnostics logs.")
    }

    return $hints.ToArray()
}

function Test-WinUiToolchainPreconditions {
    param(
        [hashtable]$EnvironmentSummary,
        [hashtable]$NuGetProbe
    )

    $failedChecks = [System.Collections.Generic.List[string]]::new()
    $recommendedActions = [System.Collections.Generic.List[string]]::new()
    $warnings = [System.Collections.Generic.List[string]]::new()
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

    $msbuildDetectedPath = ""
    $msbuildCommand = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($null -ne $msbuildCommand) {
        $msbuildDetectedPath = $msbuildCommand.Source
    } else {
        $msbuildCandidates = @(
            (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"),
            (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe")
        )
        foreach ($candidate in $msbuildCandidates) {
            if (Test-Path $candidate) {
                $msbuildDetectedPath = $candidate
                break
            }
        }
    }
    $hasMsbuild = -not [string]::IsNullOrWhiteSpace($msbuildDetectedPath)
    if (-not $hasMsbuild) {
        $warnings.Add("MISSING_MSBUILD_DISCOVERY")
        $recommendedActions.Add("MSBuild.exe was not discovered from PATH or standard VS2022 locations; verify Build Tools installation if WinUI publish continues to fail.")
    }

    $windowsSdkBinProbePath = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\rc.exe"
    $hasWindowsSdkBinTool = Test-Path $windowsSdkBinProbePath
    if (-not $hasWindowsSdkBinTool) {
        $failedChecks.Add("MISSING_WINDOWS_SDK_19041_BINTOOLS")
        $recommendedActions.Add("Install Windows 10 SDK 10.0.19041.0 C++/bin tools (rc.exe) required by WinUI toolchain.")
    }

    $windowsAppSdkVersion = ""
    if (Test-Path $winUiProject) {
        $packageLine = Select-String -Path $winUiProject -Pattern 'PackageReference Include="Microsoft.WindowsAppSDK"' -SimpleMatch | Select-Object -First 1
        if ($null -ne $packageLine -and $packageLine.Line -match 'Version="([^"]+)"') {
            $windowsAppSdkVersion = $Matches[1]
        }
    }
    $windowsAppSdkPackagePath = ""
    if (-not [string]::IsNullOrWhiteSpace($windowsAppSdkVersion)) {
        $windowsAppSdkPackagePath = Join-Path $env:USERPROFILE ".nuget\packages\microsoft.windowsappsdk\$windowsAppSdkVersion"
    }
    $hasWindowsAppSdkPackage = (-not [string]::IsNullOrWhiteSpace($windowsAppSdkPackagePath)) -and (Test-Path $windowsAppSdkPackagePath)
    if (-not $hasWindowsAppSdkPackage) {
        $failedChecks.Add("MISSING_WINDOWSAPPSDK_PACKAGE_CACHE")
        $recommendedActions.Add("Run dotnet restore for WinUiHost and verify Microsoft.WindowsAppSDK package cache is available.")
    }
    if ($null -ne $NuGetProbe -and $null -ne $NuGetProbe.summary) {
        if ([int]$NuGetProbe.summary.enabled -gt 0 -and [int]$NuGetProbe.summary.reachable -eq 0) {
            $warnings.Add("NO_REACHABLE_NUGET_SOURCE")
            $recommendedActions.Add("No enabled NuGet source responded to HTTP probe. Verify source URL/auth/proxy before WinUI publish.")
        }
    }

    return [ordered]@{
        passed = ($failedChecks.Count -eq 0)
        failed_checks = $failedChecks.ToArray()
        warnings = $warnings.ToArray()
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
                },
                [ordered]@{
                    check = "MSBUILD_DISCOVERY"
                    detected = $hasMsbuild
                    detected_path = $msbuildDetectedPath
                },
                [ordered]@{
                    check = "WINDOWS_SDK_19041_BINTOOLS"
                    detected = $hasWindowsSdkBinTool
                    checked_paths = @($windowsSdkBinProbePath)
                    detected_path = if ($hasWindowsSdkBinTool) { $windowsSdkBinProbePath } else { "" }
                },
                [ordered]@{
                    check = "WINDOWSAPPSDK_PACKAGE_CACHE"
                    detected = $hasWindowsAppSdkPackage
                    checked_paths = if (-not [string]::IsNullOrWhiteSpace($windowsAppSdkPackagePath)) { @($windowsAppSdkPackagePath) } else { @() }
                    expected_version = $windowsAppSdkVersion
                }
            )
        }
    }
}

function Get-WinUiFailureClass {
    param(
        [string]$DiagLogPath,
        [string]$ManagedDiagLogPath,
        [hashtable]$Preflight,
        [string]$ObjRoot
    )

    if ($null -ne $Preflight -and $null -ne $Preflight.passed -and -not [bool]$Preflight.passed) {
        $checks = @($Preflight.failed_checks)
        if ($checks -contains "MISSING_DOTNET_8_SDK") {
            return [ordered]@{ Class = "TOOLCHAIN_MISSING_DOTNET8"; Confidence = "high" }
        }
        if ($checks -contains "MISSING_WINDOWS_SDK_19041_METADATA" -or $checks -contains "MISSING_WINDOWS_SDK_19041_BINTOOLS") {
            return [ordered]@{ Class = "TOOLCHAIN_WINDOWS_SDK_INCOMPLETE"; Confidence = "high" }
        }
        if ($checks -contains "MISSING_WINDOWSAPPSDK_PACKAGE_CACHE") {
            return [ordered]@{ Class = "WINDOWSAPPSDK_RESTORE_INCOMPLETE"; Confidence = "high" }
        }
        if ($checks -contains "MISSING_VISUAL_STUDIO_DISCOVERY" -or $checks -contains "MISSING_MSBUILD_DISCOVERY") {
            return [ordered]@{ Class = "TOOLCHAIN_VISUAL_STUDIO_INCOMPLETE"; Confidence = "high" }
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
        if ((Select-String -Path $DiagLogPath -Pattern "401" -SimpleMatch -Quiet) -or
            (Select-String -Path $DiagLogPath -Pattern "403" -SimpleMatch -Quiet)) {
            return [ordered]@{ Class = "NUGET_AUTH_FAILURE"; Confidence = "medium" }
        }
        if ((Select-String -Path $DiagLogPath -Pattern "NU1101" -SimpleMatch -Quiet) -or
            (Select-String -Path $DiagLogPath -Pattern "NU1301" -SimpleMatch -Quiet)) {
            return [ordered]@{ Class = "NUGET_SOURCE_UNREACHABLE"; Confidence = "high" }
        }
        if ((Select-String -Path $DiagLogPath -Pattern "MSB3073" -SimpleMatch -Quiet) -and
            (Select-String -Path $DiagLogPath -Pattern "XamlCompiler.exe" -SimpleMatch -Quiet)) {
            $inputPath = Join-Path $ObjRoot "x64\Release\net8.0-windows10.0.19041.0\input.json"
            $outputPath = Join-Path $ObjRoot "x64\Release\net8.0-windows10.0.19041.0\output.json"
            if ((Test-Path $inputPath) -and -not (Test-Path $outputPath)) {
                return [ordered]@{ Class = "TOOLCHAIN_XAML_PLATFORM_UNSUPPORTED"; Confidence = "high" }
            }
            return [ordered]@{ Class = "XAML_COMPILER_EXEC_FAIL"; Confidence = "high" }
        }
    }

    return [ordered]@{ Class = "UNKNOWN"; Confidence = "low" }
}

function Collect-WinUiDiagnostics {
    param(
        [string]$Reason,
        [string]$PublishError,
        [hashtable]$Preflight,
        [hashtable]$NuGetProbe
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

    $envSummary = Get-WinUiEnvironmentSummary
    $dotnetInfo = Get-DotNetVersionInfo -WorkingDirectory $repoRoot
    $profiles = [System.Collections.Generic.List[object]]::new()

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
    Push-Location $repoRoot
    try {
        & dotnet @diagArgs 1> $diagLogPath 2> $diagStderrPath
    } finally {
        Pop-Location
    }
    $diagExitCode = $LASTEXITCODE
    $diagProfileHints = Get-WinUiProfileHints -ProfileName "diag-default" -DiagLogPath $diagLogPath -ManagedDiagLogPath $managedDiagLogPath
    $profiles.Add([ordered]@{
        name = "diag-default"
        enabled = $true
        command = $diagCommandText
        exit_code = $diagExitCode
        artifacts = [ordered]@{
            binlog = $binlogPath
            diag_log = $diagLogPath
            stderr_log = $diagStderrPath
        }
        root_cause_hints = $diagProfileHints
    })

    $managedDiagCommandText = ""
    $managedDiagExitCode = -1
    $shouldRunManagedDiagnostics = $CollectManagedXamlDiagnostics -and $WinUiDiagnosticsProfile -eq "full"
    if ($shouldRunManagedDiagnostics) {
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
        Push-Location $repoRoot
        try {
            & dotnet @managedDiagArgs 1> $managedDiagLogPath 2> $managedDiagStderrPath
        } finally {
            Pop-Location
        }
        $managedDiagExitCode = $LASTEXITCODE

        $managedProfileHints = Get-WinUiProfileHints -ProfileName "managed-xaml" -DiagLogPath $diagLogPath -ManagedDiagLogPath $managedDiagLogPath
        $profiles.Add([ordered]@{
            name = "managed-xaml"
            enabled = $true
            command = $managedDiagCommandText
            exit_code = $managedDiagExitCode
            artifacts = [ordered]@{
                diag_log = $managedDiagLogPath
                stderr_log = $managedDiagStderrPath
            }
            root_cause_hints = $managedProfileHints
        })
    } else {
        $profiles.Add([ordered]@{
            name = "managed-xaml"
            enabled = $false
            skipped_reason = if (-not $CollectManagedXamlDiagnostics) { "CollectManagedXamlDiagnostics=false" } else { "WinUiDiagnosticsProfile=diag-only" }
        })
    }

    Copy-WinUiObjDiagnostics -ObjRoot $objRoot -ObjDumpRoot $objDumpPath
    $rootCauseHints = Get-WinUiRootCauseHints -DiagLogPath $diagLogPath -ManagedDiagLogPath $managedDiagLogPath -EnvironmentSummary $envSummary -NuGetProbe $NuGetProbe
    $failure = Get-WinUiFailureClass -DiagLogPath $diagLogPath -ManagedDiagLogPath $managedDiagLogPath -Preflight $Preflight -ObjRoot $objRoot
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
        -PreflightProbe $Preflight.probe `
        -Profiles $profiles.ToArray() `
        -NuGetProbe $NuGetProbe `
        -DotNetVersion $dotnetInfo.version `
        -SdkResolutionContext ([ordered]@{ global_json_root = $repoRoot; diagnostics_workdir = $dotnetInfo.working_directory })

    $log.Add("WinUI diagnostics command: $diagCommandText")
    $log.Add("WinUI diagnostics exit code: $diagExitCode")
    if ($shouldRunManagedDiagnostics) {
        $log.Add("WinUI managed diagnostics command: $managedDiagCommandText")
        $log.Add("WinUI managed diagnostics exit code: $managedDiagExitCode")
        $log.Add("WinUI managed diagnostics log: $managedDiagLogPath")
        $log.Add("WinUI managed diagnostics stderr log: $managedDiagStderrPath")
    } else {
        $log.Add("WinUI managed diagnostics: skipped (profile=$WinUiDiagnosticsProfile, collect_flag=$CollectManagedXamlDiagnostics)")
    }
    foreach ($profile in $profiles) {
        $profileName = "$($profile.name)"
        $profileEnabled = "$($profile.enabled)"
        $log.Add("WinUI diagnostics profile: $profileName enabled=$profileEnabled")
        if ($profile.Contains("exit_code")) {
            $log.Add("WinUI diagnostics profile exit code [$profileName]: $($profile.exit_code)")
        }
        if ($profile.Contains("root_cause_hints")) {
            foreach ($hint in @($profile.root_cause_hints)) {
                $log.Add("WinUI diagnostics profile hint [$profileName]: $hint")
            }
        }
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
    if ($null -ne $NuGetProbe -and $null -ne $NuGetProbe.summary) {
        $log.Add("NuGet probe summary: enabled=$($NuGetProbe.summary.enabled), reachable=$($NuGetProbe.summary.reachable), unreachable=$($NuGetProbe.summary.unreachable), unknown=$($NuGetProbe.summary.unknown)")
    }
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
    Invoke-DotNetCommandWithRetry -Description "WPF publish" -Args $wpfPublishArgs -RetryCount $WinUiRestoreRetryCount

    if (-not (Test-Path (Join-Path $wpfDist "WpfHost.exe"))) {
        throw "WPF publish output not found: $wpfDist"
    }

    Copy-Item -Path $nativeCoreDll -Destination $wpfDist -Force
    Assert-NativeCoreCopyIntegrity -SourcePath $nativeCoreDll -DestinationPath (Join-Path $wpfDist "nativecore.dll") -Label "WPF" -Log $log
    Copy-SpoutRuntimeBinaries -RepoRoot $repoRoot -DistDir $wpfDist -Log $log
    $log.Add("WPF dist: $wpfDist")
    $log.Add("WPF exe: $(Join-Path $wpfDist 'WpfHost.exe')")
    if ($RunWpfLaunchSmoke) {
        if (-not (Test-Path $wpfLaunchSmokeScript)) {
            $log.Add("WPF launch smoke: skipped (script not found: $wpfLaunchSmokeScript)")
        } else {
            Write-Step "Running WPF launch smoke probe..."
            $smokeResult = & $wpfLaunchSmokeScript `
                -ExePath (Join-Path $wpfDist "WpfHost.exe") `
                -WorkingDirectory $wpfDist `
                -AliveSeconds $WpfLaunchSmokeDurationSeconds `
                -ReportPath $resolvedWpfLaunchSmokeReportPath `
                -TreatFailureAsError:$WpfLaunchSmokeFailOnError
            if ($null -ne $smokeResult -and "$($smokeResult.status)" -eq "PASS") {
                $log.Add("WPF launch smoke: PASS")
            } else {
                $reportedExit = if ($null -ne $smokeResult -and $smokeResult.Contains("exit_code")) { "$($smokeResult.exit_code)" } else { "unknown" }
                $log.Add("WPF launch smoke: FAIL (exit=$reportedExit)")
            }
            $log.Add("WPF launch smoke report: $resolvedWpfLaunchSmokeReportPath")
        }
    } else {
        $log.Add("WPF launch smoke: skipped (RunWpfLaunchSmoke=false)")
    }
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
    $nugetProbe = Get-NuGetSourceProbe -TimeoutSeconds $NuGetProbeTimeoutSeconds
    $winUiPreflight = Test-WinUiToolchainPreconditions -EnvironmentSummary $envSummaryForPreflight -NuGetProbe $nugetProbe
    if ($null -ne $nugetProbe -and $null -ne $nugetProbe.summary) {
        $log.Add("NuGet probe summary: enabled=$($nugetProbe.summary.enabled), reachable=$($nugetProbe.summary.reachable), unreachable=$($nugetProbe.summary.unreachable), unknown=$($nugetProbe.summary.unknown)")
    }
    if (-not [bool]$winUiPreflight.passed) {
        $preflightError = "WinUI toolchain preflight failed: $($winUiPreflight.failed_checks -join ', ')"
        $log.Add("WinUI preflight: FAIL")
        foreach ($check in $winUiPreflight.failed_checks) {
            $log.Add("WinUI preflight failed check: $check")
        }
        foreach ($action in $winUiPreflight.recommended_actions) {
            $log.Add("WinUI preflight recommended action: $action")
        }
        foreach ($warning in @($winUiPreflight.warnings)) {
            $log.Add("WinUI preflight warning: $warning")
        }

        if ($CollectWinUiDiagnostics) {
            Write-Step "WinUI preflight failed; collecting diagnostics..."
            Collect-WinUiDiagnostics -Reason "winui preflight failed" -PublishError $preflightError -Preflight $winUiPreflight -NuGetProbe $nugetProbe
            Write-Step "WinUI diagnostics saved under: $resolvedWinUiDiagDir"
        } else {
            $log.Add("WinUI diagnostics: skipped (CollectWinUiDiagnostics=false)")
        }

        $log | Set-Content -Path $logPath -Encoding UTF8
        throw $preflightError
    }

    $log.Add("WinUI preflight: PASS")
    foreach ($warning in @($winUiPreflight.warnings)) {
        $log.Add("WinUI preflight warning: $warning")
    }
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
        Invoke-DotNetCommandWithRetry -Description "WinUI publish" -Args $winUiPublishArgs -RetryCount $WinUiRestoreRetryCount

        if (-not (Test-Path (Join-Path $winUiDist "WinUiHost.exe"))) {
            throw "WinUI publish output not found: $winUiDist"
        }
        Copy-Item -Path $nativeCoreDll -Destination $winUiDist -Force
        Assert-NativeCoreCopyIntegrity -SourcePath $nativeCoreDll -DestinationPath (Join-Path $winUiDist "nativecore.dll") -Label "WinUI" -Log $log
        Copy-SpoutRuntimeBinaries -RepoRoot $repoRoot -DistDir $winUiDist -Log $log
        $log.Add("WinUI dist: $winUiDist")
        $log.Add("WinUI exe: $(Join-Path $winUiDist 'WinUiHost.exe')")
    } catch {
        $publishErrorText = $_ | Out-String
        $log.Add("WinUI publish: failed")
        $log.Add("WinUI publish error: $($publishErrorText.Trim())")

        if ($CollectWinUiDiagnostics) {
            Write-Step "WinUI publish failed; collecting diagnostics..."
            Collect-WinUiDiagnostics -Reason "winui publish failed" -PublishError $publishErrorText -Preflight $winUiPreflight -NuGetProbe $nugetProbe
            Write-Step "WinUI diagnostics saved under: $resolvedWinUiDiagDir"
        } else {
            $log.Add("WinUI diagnostics: skipped (CollectWinUiDiagnostics=false)")
        }

        $log | Set-Content -Path $logPath -Encoding UTF8
        throw
    }
} else {
    $log.Add("WinUI publish: skipped (WPF_ONLY mode; use -IncludeWinUi for optional diagnostics track)")
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
