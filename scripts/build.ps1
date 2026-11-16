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
. (Join-Path $PSScriptRoot 'tools.ps1')
$Bruce = Join-Path $Root 'bruce'
$Py    = Join-Path $ToolsDir 'python\tools\python.exe'

if (-not (Test-Path $Py)) { throw 'Toolchain missing: run .\scripts\setup.ps1 first.' }
if (-not (Test-Path "$Bruce\platformio.ini")) { throw 'Bruce submodule missing: run .\scripts\setup.ps1 first.' }

# Everything (PlatformIO core, toolchains, libdeps, build) lives in .tools\ (git-ignored).
# Some library files sit ~190 characters deep under libdeps: with MAX_PATH = 260 the repo
# itself must stay on a short path (e.g. C:\dev\idfm).
if ($Root.Length -gt 60) {
    Write-Warning "Repo path is $($Root.Length) chars long: the build may hit the Windows 260-char path limit. Prefer a short path such as C:\dev\idfm."
}
Set-PlatformIOEnv
$Ws = $env:PLATFORMIO_WORKSPACE_DIR
New-Item -ItemType Directory -Force $Ws | Out-Null
# Bruce's scripts hard-code `.pio/libdeps`: bruce\.pio is a junction to the workspace.
$dotPio = Join-Path $Bruce '.pio'
$item = Get-Item $dotPio -Force -ErrorAction SilentlyContinue
if ($item -and -not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    Remove-Item -Recurse -Force $dotPio
} elseif ($item -and $item.Target -and ($item.Target | Select-Object -First 1) -ne $Ws) {
    $item.Delete()   # stale junction (repo moved / old location): remove the link only
}
if (-not (Test-Path $dotPio)) { New-Item -ItemType Junction -Path $dotPio -Target $Ws | Out-Null }
Write-Host "==> PlatformIO core: $env:PLATFORMIO_CORE_DIR"

# ESP-IDF tools refuse to install when MSYS/MinGW vars are present (inherited from Git Bash terminals)
Get-ChildItem env: | Where-Object { $_.Name -match '^(MSYSTEM.*|MINGW_.*)$' } | ForEach-Object {
    Remove-Item "env:$($_.Name)"
}
# Bruce's build scripts call `pio` directly, but pio.exe can be blocked by local policy
# ("Access is denied"): put a pio.cmd shim running `python -m platformio` first in PATH.
$shim = Join-Path $ToolsDir 'shim'
New-Item -ItemType Directory -Force $shim | Out-Null
Set-Content -Encoding ascii (Join-Path $shim 'pio.cmd') "@`"$Py`" -m platformio %*"
$env:PATH = "$shim;" + (Join-Path $ToolsDir 'python\tools\Scripts') + ';' + $env:PATH

# Bruce's patch.py weakens a symbol of libnet80211.a via `pio pkg exec`. If that call failed,
# the original is left renamed to .old with the ".patched" flag set: redo it with objcopy.
$pkgs    = Join-Path $env:PLATFORMIO_CORE_DIR 'packages'
$fwLib   = Join-Path $pkgs 'framework-arduinoespressif32-libs\esp32s3\lib'
$objcopy = Join-Path $pkgs 'toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-objcopy.exe'
if ((Test-Path "$fwLib\libnet80211.a.old") -and -not (Test-Path "$fwLib\libnet80211.a") -and (Test-Path $objcopy)) {
    & $objcopy --weaken-symbol=ieee80211_raw_frame_sanity_check "$fwLib\libnet80211.a.old" "$fwLib\libnet80211.a"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to repair libnet80211.a' }
    Write-Host '==> Repaired libnet80211.a (Bruce patch.py)'
}

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
# python -m platformio: pio.exe launcher can be blocked by local policy.
# 'Continue': when output is redirected, PS 5.1 would turn pio's stderr into a fatal error.
$ErrorActionPreference = 'Continue'
& $Py -m platformio @pioArgs
exit $LASTEXITCODE
