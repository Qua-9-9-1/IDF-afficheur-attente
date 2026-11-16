<#
.SYNOPSIS
    Repo-local toolchain helpers, dot-sourced by setup.ps1 / build.ps1 / sim.ps1.
    Every tool lives under <repo>\.tools (git-ignored): nothing is installed on the machine.
      .tools\python   portable CPython (official NuGet package, no installer)
      .tools\pio      PlatformIO core dir (toolchains, framework, penv)
      .tools\w        PlatformIO workspace (libdeps, build, caches) - short name for MAX_PATH
      .tools\sim      simulator dependencies and build output
#>

$Root     = Split-Path -Parent $PSScriptRoot
$ToolsDir = Join-Path $Root '.tools'
$PythonVersion = '3.12.10'

# Windows PowerShell 5.1 turns any stderr line of a native tool into a terminating error under
# 'Stop': scripts use 'Continue' and check native exit codes with Assert-Exit.
function Assert-Exit([string]$What) {
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

# Returns the repo-local python.exe, downloading the portable CPython on first use.
function Get-LocalPython {
    $dir = Join-Path $ToolsDir 'python'
    $exe = Join-Path $dir 'tools\python.exe'
    if (Test-Path $exe) { return $exe }

    Write-Host "==> Downloading portable Python $PythonVersion into .tools\python"
    New-Item -ItemType Directory -Force $dir | Out-Null
    $pkg = Join-Path $ToolsDir "python-$PythonVersion.zip"
    Invoke-WebRequest -UseBasicParsing -ErrorAction Stop -OutFile $pkg `
        -Uri "https://www.nuget.org/api/v2/package/python/$PythonVersion"
    Expand-Archive -Path $pkg -DestinationPath $dir -Force -ErrorAction Stop
    Remove-Item $pkg
    if (-not (Test-Path $exe)) { throw "Portable Python not found at $exe" }
    & $exe -m ensurepip --upgrade 2>&1 | Out-Null
    Assert-Exit 'ensurepip'
    return $exe
}

# Installs (if missing) a pip package into the repo-local Python. $Module is the import name.
function Install-PythonPackage([string]$Python, [string]$Package, [string]$Module) {
    & $Python -c "import $Module" 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { return }
    Write-Host "==> Installing $Package into .tools\python"
    & $Python -m pip install -q --disable-pip-version-check $Package 2>&1 | ForEach-Object { "$_" }
    Assert-Exit "pip install $Package"
}

# Environment for PlatformIO: core dir and workspace inside the repo.
function Set-PlatformIOEnv {
    $env:PLATFORMIO_CORE_DIR        = Join-Path $ToolsDir 'pio'
    $env:PLATFORMIO_WORKSPACE_DIR   = Join-Path $ToolsDir 'w'
    $env:PLATFORMIO_CACHE_DIR       = Join-Path $ToolsDir 'w\c'
    $env:PLATFORMIO_BUILD_CACHE_DIR = Join-Path $ToolsDir 'w\bc'
    $env:PLATFORMIO_BUILD_DIR       = Join-Path $ToolsDir 'w\b'
}
