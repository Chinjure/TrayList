# TrayVerticalList Windows build script
# Downloads CMake if needed, then builds the native project

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $ProjectDir "build"

Write-Host "=== TrayVerticalList Native Build ===" -ForegroundColor Cyan

# 1. Locate or install CMake
$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    Write-Host "CMake not found, downloading portable version..." -ForegroundColor Yellow
    $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v4.0.1/cmake-4.0.1-windows-x86_64.zip"
    $cmakeZip = Join-Path $env:TEMP "cmake-portable.zip"
    $cmakeDir = Join-Path $env:LOCALAPPDATA "cmake-portable"

    if (-not (Test-Path $cmakeDir)) {
        Write-Host "Downloading CMake..."
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $cmakeUrl -OutFile $cmakeZip -UseBasicParsing
        Write-Host "Extracting CMake..."
        Expand-Archive -Path $cmakeZip -DestinationPath $cmakeDir -Force
        Remove-Item $cmakeZip
    }
    $cmakeExe = Join-Path $cmakeDir "cmake-4.0.1-windows-x86_64" "bin" "cmake.exe"
    if (-not (Test-Path $cmakeExe)) {
        # Try to find the actual cmake.exe
        $cmakeExe = Get-ChildItem -Path $cmakeDir -Recurse -Filter "cmake.exe" | Select-Object -First 1 -ExpandProperty FullName
    }
    Write-Host "CMake: $cmakeExe"
} else {
    $cmakeExe = $cmake.Source
    Write-Host "CMake found: $cmakeExe"
}

if (-not (Test-Path $cmakeExe)) {
    Write-Host "ERROR: Could not find or install CMake" -ForegroundColor Red
    Write-Host "Please install CMake manually from https://cmake.org/download/" -ForegroundColor Red
    pause
    exit 1
}

# 2. Check for C++ compiler (MSVC or Clang)
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcVars = $null
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath 2>$null
    if ($vsPath) {
        $vcVars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        Write-Host "Visual Studio found: $vsPath"
    }
}

if (-not $vcVars) {
    Write-Host "ERROR: Visual Studio with C++ tools not found!" -ForegroundColor Red
    Write-Host "Please install Visual Studio 2022 with 'Desktop development with C++' workload" -ForegroundColor Red
    Write-Host "Download: https://visualstudio.microsoft.com/downloads/" -ForegroundColor Red
    pause
    exit 1
}

# 3. Build
Write-Host "`nConfiguring project..." -ForegroundColor Cyan
Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue

# Need to run cmake from within VS dev environment
$buildScript = @"
@echo off
call "$vcVars" >nul 2>&1
cd /d "$ProjectDir"
"$cmakeExe" -S . -B "$BuildDir" -G "Visual Studio 17 2022" -A x64
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
"$cmakeExe" --build "$BuildDir" --config Release
"@

$batFile = Join-Path $env:TEMP "tvl_build.bat"
$buildScript | Out-File -FilePath $batFile -Encoding ASCII

Write-Host "Running build via VS Developer Command Prompt..." -ForegroundColor Cyan
Push-Location $ProjectDir
$result = cmd /c $batFile 2>&1
$exitCode = $LASTEXITCODE
Pop-Location
Remove-Item $batFile -ErrorAction SilentlyContinue

Write-Host $result
if ($exitCode -eq 0) {
    $exe = Join-Path $BuildDir "Release" "TrayVerticalList.exe"
    if (Test-Path $exe) {
        Write-Host "`n=== BUILD SUCCESS ===" -ForegroundColor Green
        Write-Host "Output: $exe" -ForegroundColor Green
    } else {
        Write-Host "`nBuild completed but exe not found" -ForegroundColor Yellow
    }
} else {
    Write-Host "`n=== BUILD FAILED (exit code: $exitCode) ===" -ForegroundColor Red
}

pause
