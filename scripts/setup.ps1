<#
.SYNOPSIS
    One-time toolchain setup, entirely inside the repo (.tools\, git-ignored):
    portable Python, PlatformIO, Bruce submodule. Nothing is installed on the machine.
#>
$ErrorActionPreference = 'Continue' # see Assert-Exit in tools.ps1
. (Join-Path $PSScriptRoot 'tools.ps1')
Set-Location $Root

$py = Get-LocalPython
Write-Host "==> Python: $py"
Install-PythonPackage $py 'platformio' 'platformio'

Write-Host '==> Fetching Bruce submodule'
git submodule update --init --depth 1 bruce 2>&1 | ForEach-Object { "$_" }
Assert-Exit 'Fetching the Bruce submodule'

Write-Host '==> Done. Next: .\scripts\build.ps1 (firmware) or .\scripts\sim.ps1 (browser simulator)'
