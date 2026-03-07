# Install Windows 10 SDK (10.0.19041.0) for WinUI Build
# This script requires Administrator privileges.

$sdkId = "Microsoft.WindowsSDK.10.0.19041.0"

Write-Host "[setup] Checking for Windows 10 SDK (10.0.19041.0)..." -ForegroundColor Cyan

# Check if winget is available
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Error "winget not found. Please install App Installer from Microsoft Store."
    exit 1
}

# Check if already installed
$installed = winget list --id $sdkId --source winget
if ($installed -match $sdkId) {
    Write-Host "[setup] SDK 10.0.19041.0 is already installed." -ForegroundColor Green
    exit 0
}

Write-Host "[setup] Installing Windows 10 SDK (10.0.19041.0) via winget..." -ForegroundColor Yellow
winget install --id $sdkId --silent --accept-package-agreements --accept-source-agreements

if ($LASTEXITCODE -eq 0) {
    Write-Host "[setup] SDK installation successful. Please restart your terminal/IDE." -ForegroundColor Green
} else {
    Write-Error "[setup] SDK installation failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}
