param(
    [string]$Root = "."
)

$ErrorActionPreference = "Stop"

function Test-Utf8Bom {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 3) {
        return $false
    }

    return ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
}

function Test-Mojibake {
    param([string]$Text)
    $replacementChar = [string][char]0xFFFD
    if ($Text.Contains($replacementChar)) {
        return $true
    }

    # Heuristic markers commonly produced by mojibake in UI labels.
    if ($Text -match "\?[^\s`"'<>=/]{1,6}") {
        return $true
    }

    if ($Text -match "[^\s`"'<>=/]{1,6}\?") {
        return $true
    }

    return $false
}

$targetPatterns = @(
    "NativeVsfClone/host/WpfHost/*.xaml",
    "NativeVsfClone/host/WpfHost/*.cs",
    "NativeVsfClone/host/WinUiHost/*.xaml",
    "NativeVsfClone/host/WinUiHost/*.cs"
)

$failed = 0

foreach ($pattern in $targetPatterns) {
    $files = Get-ChildItem -Path (Join-Path $Root $pattern) -File -ErrorAction SilentlyContinue
    foreach ($file in $files) {
        $path = $file.FullName
        $text = [System.IO.File]::ReadAllText($path)

        if (-not (Test-Utf8Bom -Path $path)) {
            Write-Host "[FAIL] BOM missing: $path"
            $failed++
            continue
        }

        $isXaml = [string]::Equals($file.Extension, ".xaml", [System.StringComparison]::OrdinalIgnoreCase)
        if ($isXaml -and (Test-Mojibake -Text $text)) {
            Write-Host "[FAIL] Mojibake pattern detected: $path"
            $failed++
            continue
        }

        Write-Host "[OK] $path"
    }
}

if ($failed -gt 0) {
    Write-Error ("UI encoding check failed ({0} file(s))." -f $failed)
    exit 1
}

Write-Host "UI encoding check passed."
