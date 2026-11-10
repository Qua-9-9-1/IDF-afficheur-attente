<#
.SYNOPSIS
    One-time local toolchain setup: Python, PlatformIO (in .venv), Bruce submodule.
    Nothing installed here is committed.
#>
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Find-Python {
    # Ignore the Microsoft Store alias (WindowsApps\python.exe)
    foreach ($cmd in @('py', 'python')) {
        $c = Get-Command $cmd -ErrorAction SilentlyContinue
        if ($c -and $c.Source -notmatch 'WindowsApps') { return $c.Source }
    }
    $candidates = Get-ChildItem "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe" -ErrorAction SilentlyContinue
    if ($candidates) { return ($candidates | Sort-Object FullName -Descending | Select-Object -First 1).FullName }
    return $null
}

$py = Find-Python
if (-not $py) {
    Write-Host '==> Installing Python 3.12 (winget, user scope)'
    winget install --id Python.Python.3.12 -e --scope user --accept-source-agreements --accept-package-agreements
    $py = Find-Python
    if (-not $py) { throw 'Python not found after install; open a new terminal and re-run.' }
}
Write-Host "==> Python: $py"

if (-not (Test-Path "$Root\.venv\Scripts\python.exe")) {
    Write-Host '==> Creating .venv'
    & $py -m venv "$Root\.venv"
}
$venvPy = "$Root\.venv\Scripts\python.exe"
& $venvPy -m pip install --upgrade pip
& $venvPy -m pip install --upgrade platformio

Write-Host '==> Fetching Bruce submodule'
git submodule update --init --depth 1 bruce

Write-Host '==> Done. Next: .\scripts\build.ps1'
