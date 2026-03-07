# WinUI Build Environment Setup Guide

To resolve `WMC9999` (XamlCompiler.exe exit code 1) and `MISSING_WINDOWS_SDK_19041_METADATA` errors, ensure the following environment requirements are met.

## 1. Prerequisites (Local Dev Machine)
- **Visual Studio 2022** (v17.4 or higher)
- **Workloads:**
  - [.NET Desktop Development]
  - [Windows Application Development]
- **Individual Components:**
  - [Windows 10 SDK (10.0.19041.0)] - **CRITICAL**
  - [C++ (v143) Universal Windows Platform tools]

## 2. CI Environment Setup (GitHub Actions)
Add the following step to your workflow before `dotnet build`:

```yaml
- name: Setup Windows SDK
  uses: microsoft/setup-msbuild@v1.1

- name: Install SDK via winget (if missing)
  run: |
    winget install --id Microsoft.WindowsSDK.10.0.19041.0 --silent --accept-package-agreements
```

## 3. Build Configuration
Ensure `WinUiHost.csproj` has the following properties:
- `<TargetFramework>net8.0-windows10.0.19041.0</TargetFramework>`
- `<TargetPlatformMinVersion>10.0.19041.0</TargetPlatformMinVersion>`

## 4. Troubleshooting
If the error persists:
1. Delete `bin/` and `obj/` folders.
2. Run `dotnet restore --force`.
3. Verify that `C:\Program Files (x86)\Windows Kits\10\Platforms\UAP\10.0.19041.0\Platform.xml` exists.
