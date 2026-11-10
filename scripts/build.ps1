<#
.SYNOPSIS
    Integrates app/idfm into the Bruce submodule and builds/flashes it with PlatformIO.
.EXAMPLE
    .\scripts\build.ps1            # build
    .\scripts\build.ps1 -Upload    # build + flash
    .\scripts\build.ps1 -Monitor   # serial monitor
    .\scripts\build.ps1 -Clean     # clean build
#>
param(
    [string]$Env = 'lilygo-t-embed-cc1101',
    [switch]$Upload,
    [switch]$Monitor,
    [switch]$Clean,
    [string]$Port
)
$ErrorActionPreference = 'Stop'
$Root  = Split-Path -Parent $PSScriptRoot
$Bruce = Join-Path $Root 'bruce'
$Py    = Join-Path $Root '.venv\Scripts\python.exe'

if (-not (Test-Path $Py)) { throw 'PlatformIO missing: run .\scripts\setup.ps1 first.' }
if (-not (Test-Path "$Bruce\platformio.ini")) { throw 'Bruce submodule missing: run .\scripts\setup.ps1 first.' }

# PlatformIO workspace (cache, libdeps, build) outside OneDrive: avoids the Windows
# 260-char path limit and OneDrive syncing gigabytes of build files.
# Bruce's scripts hard-code `.pio/libdeps`, so bruce/.pio is kept but made a junction to it.
$Ws = Join-Path $env:LOCALAPPDATA 'idfm\ws'
New-Item -ItemType Directory -Force $Ws | Out-Null
$dotPio = Join-Path $Bruce '.pio'
if ((Test-Path $dotPio) -and -not ((Get-Item $dotPio -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    Remove-Item -Recurse -Force $dotPio
}
if (-not (Test-Path $dotPio)) { New-Item -ItemType Junction -Path $dotPio -Target $Ws | Out-Null }
$env:PLATFORMIO_CACHE_DIR       = Join-Path $Ws 'cache'        # short paths for package extraction
$env:PLATFORMIO_BUILD_CACHE_DIR = Join-Path $Ws 'buildcache'
$env:PLATFORMIO_BUILD_DIR       = Join-Path $Ws 'build'

# ESP-IDF tools refuse to install when MSYS/MinGW vars are present (inherited from Git Bash terminals)
Get-ChildItem env: | Where-Object { $_.Name -match '^(MSYSTEM.*|MINGW_.*)$' } | ForEach-Object {
    Remove-Item "env:$($_.Name)"
}
# Bruce's build scripts call `pio` directly
$env:PATH = (Join-Path $Root '.venv\Scripts') + ';' + $env:PATH

# 1. Link our module into Bruce's source tree
$link = Join-Path $Bruce 'src\modules\idfm'
if (-not (Test-Path $link)) {
    New-Item -ItemType Junction -Path $link -Target (Join-Path $Root 'app\idfm') | Out-Null
    Write-Host "==> Linked $link"
}

# 2. Apply patches (idempotent)
Get-ChildItem (Join-Path $Root 'patches') -Filter *.patch -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object {
    $ErrorActionPreference = 'Continue'
    git -C $Bruce apply --reverse --check $_.FullName 2>&1 | Out-Null
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -eq 0) { Write-Host "==> Already applied: $($_.Name)"; return }
    git -C $Bruce apply $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "Patch failed: $($_.Name)" }
    Write-Host "==> Applied: $($_.Name)"
}

# 3. PlatformIO
$pioArgs = @('run', '-d', $Bruce, '-e', $Env)
if ($Clean)   { $pioArgs += @('-t', 'clean') }
if ($Upload)  { $pioArgs += @('-t', 'upload') }
if ($Port)    { $pioArgs += @('--upload-port', $Port) }
if ($Monitor -and -not $Upload -and -not $Clean) {
    $pioArgs = @('device', 'monitor', '-d', $Bruce, '-e', $Env)
    if ($Port) { $pioArgs += @('-p', $Port) }
}
Write-Host "==> pio $($pioArgs -join ' ')"
# python -m platformio: pio.exe launcher can be blocked by local policy
& $Py -m platformio @pioArgs
exit $LASTEXITCODE
