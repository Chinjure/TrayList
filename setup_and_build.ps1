# TrayVerticalList - Setup & Build (一键安装 + 编译)
# 需要管理员权限安装 Build Tools

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "=== TrayVerticalList C++ Build Setup ===" -ForegroundColor Cyan

# Step 1: Check VS / Install Build Tools
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath 2>$null
}

if (-not $vsPath) {
    Write-Host "`nVisual Studio not found. Installing Build Tools..." -ForegroundColor Yellow
    Write-Host "This requires admin privileges and ~3GB download." -ForegroundColor Yellow

    # Download VS Build Tools installer
    $installer = "$env:TEMP\vs_buildtools.exe"
    Write-Host "Downloading VS Build Tools installer..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile $installer -UseBasicParsing

    Write-Host "Installing VS Build Tools (this will take several minutes)..."
    Start-Process -FilePath $installer -ArgumentList @(
        "--quiet", "--wait", "--norestart",
        "--add", "Microsoft.VisualStudio.Workload.VCTools",
        "--add", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "--add", "Microsoft.VisualStudio.Component.Windows11SDK.22621",
        "--includeRecommended"
    ) -Wait -NoNewWindow

    Write-Host "Build Tools installed. Please restart this script." -ForegroundColor Green
    pause
    exit 0
}

Write-Host "Visual Studio: $vsPath" -ForegroundColor Green

# Step 2: Check CMake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host "CMake not found, downloading..." -ForegroundColor Yellow
    $cmakeDir = "$env:LOCALAPPDATA\cmake"
    $cmakeZip = "$env:TEMP\cmake.zip"
    if (-not (Test-Path "$cmakeDir\bin\cmake.exe")) {
        New-Item -ItemType Directory -Force -Path $cmakeDir | Out-Null
        Invoke-WebRequest -Uri "https://github.com/Kitware/CMake/releases/download/v4.0.1/cmake-4.0.1-windows-x86_64.zip" -OutFile $cmakeZip -UseBasicParsing
        Expand-Archive -Path $cmakeZip -DestinationPath $cmakeDir -Force
        Remove-Item $cmakeZip
        # cmake extracts to subdir, find it
        $sub = Get-ChildItem $cmakeDir -Directory | Select-Object -First 1
        $cmakeBin = Join-Path $sub.FullName "bin"
        Copy-Item -Path "$cmakeBin\*" -Destination $cmakeDir -Recurse -Force
    }
    $env:PATH = "$cmakeDir\bin;$env:PATH"
}
Write-Host "CMake: $(Get-Command cmake).Source" -ForegroundColor Green

# Step 3: Configure & Build
$vcVars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$BuildDir = Join-Path $ProjectDir "build"
Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue

Write-Host "`nConfiguring project with CMake..." -ForegroundColor Cyan
$bat = @"
@echo off
call "$vcVars" >nul 2>&1
cd /d "$ProjectDir"
cmake -S . -B "$BuildDir" -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 exit /b 1
cmake --build "$BuildDir" --config Release
"@
$batFile = "$env:TEMP\tvl_build.bat"
$bat | Out-File -FilePath $batFile -Encoding ASCII

$result = cmd /c $batFile 2>&1
$exitCode = $LASTEXITCODE
Remove-Item $batFile -ErrorAction SilentlyContinue

# Show result
Write-Host $result
if ($exitCode -eq 0) {
    $exe = Join-Path $BuildDir "Release" "TrayVerticalList.exe"
    if (Test-Path $exe) {
        Write-Host "`n=== BUILD SUCCESS ===" -ForegroundColor Green
        Write-Host "Output: $exe" -ForegroundColor Green
        Write-Host "Size: $((Get-Item $exe).Length / 1KB) KB"
    }
} else {
    Write-Host "`n=== BUILD FAILED ===" -ForegroundColor Red
    Write-Host "See errors above. Common fixes:"
    Write-Host "  1. Install Windows 11 SDK via Visual Studio Installer"
    Write-Host "  2. Ensure CMake finds the correct generator"
}
pause
