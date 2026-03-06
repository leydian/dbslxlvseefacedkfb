param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$ValidateUiEncoding
)

$ErrorActionPreference = "Stop"

if ($ValidateUiEncoding) {
    & "$PSScriptRoot/tools/check_ui_encoding.ps1" -Root "$PSScriptRoot/.."
}

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config $Configuration

Write-Host "Build complete:"
Write-Host "  build/$Configuration/nativecore.dll"
Write-Host "  build/$Configuration/animiq_cli.exe"
Write-Host "  build/$Configuration/avatar_tool.exe"
