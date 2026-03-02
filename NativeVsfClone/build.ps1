param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config $Configuration

Write-Host "Build complete:"
Write-Host "  build/$Configuration/nativecore.dll"
Write-Host "  build/$Configuration/vsfclone_cli.exe"
Write-Host "  build/$Configuration/avatar_tool.exe"
