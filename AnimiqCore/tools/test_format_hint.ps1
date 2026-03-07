# Test Avatar Format Hint Enforcement
# This script copies a VRM file to a wrong extension (.txt) and tries to load it using explicit VRM hint.

$repoRoot = (Get-Item -Path ".\").FullName
$buildDir = Join-Path $repoRoot "build\Release"
$cliPath = Join-Path $buildDir "animiq_cli.exe"
$sampleMiq = Join-Path $repoRoot "sample\avatar_sample.miq" 
$testFile = Join-Path $repoRoot "sample\wrong_extension.txt"

if (-not (Test-Path $cliPath)) {
    Write-Error "animiq_cli.exe not found at $cliPath"
    exit 1
}

# Create a dummy or copy existing
if (Test-Path $sampleMiq) {
    Copy-Item $sampleMiq $testFile -Force
} else {
    Write-Error "Sample MIQ not found at $sampleMiq"
    exit 1
}

Write-Host "[test] Testing Load with Wrong Extension + Explicit MIQ Hint..."
$output = & $cliPath $testFile "--format=miq" 2>&1

if ($output -match "Load succeeded") {
    Write-Host "[pass] Format hint enforced successfully!"
} else {
    Write-Host $output
    Write-Error "[fail] Format hint failed to override extension."
}

Remove-Item $testFile -ErrorAction SilentlyContinue
