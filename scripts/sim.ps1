<#
.SYNOPSIS
    Browser simulator of the IDFM board app: compiles app/idfm (unchanged) + sim/ to WebAssembly,
    then serves it locally and opens it in the browser. No device, nothing installed on the
    machine: every tool goes to <repo>\.tools (git-ignored).
.EXAMPLE
    .\scripts\sim.ps1                 # full app: RER -> line -> stop -> live board
    .\scripts\sim.ps1 -Demo           # offline RER board with sample data
    .\scripts\sim.ps1 -Demo -Board metro   # live metro demo (rer|metro|transilien|tram|bus)
    .\scripts\sim.ps1 -Demo -Board bus -Offline   # same board on sample data, no API call
    .\scripts\sim.ps1 -Demo -Shot demo.png -ExitAfter 3000   # headless capture -> .tools\sim\shots\
    .\scripts\sim.ps1 -Keys "s,w,s"   # scripted encoder: n=next p=prev s=click e=back w=wait
.NOTES
    In the page: arrows / mouse wheel = encoder, Enter / click = click, Esc / right click = back,
    P = PNG capture, R = restart. The SD card is the repo's sd/ folder: put the PRIM key in
    sd/idfm/config.json (git-ignored); the local server keeps it, the browser never sees it.
#>
param(
    [switch]$Demo,
    [ValidateSet('rer', 'metro', 'transilien', 'tram', 'bus')][string]$Board = 'rer', # live demo with -Demo
    [switch]$Offline, # with -Demo: sample data, no API call
    [string]$Keys,
    [int]$Port = 8765,
    [string]$Shot,
    [int]$ExitAfter = 0,
    [switch]$NoBrowser,
    [switch]$NoRun,
    [ValidatePattern('^[\w-]+$')][string]$Variant = 'idfm_sim' # build output name (parallel runs)
)
$ErrorActionPreference = 'Continue' # see Assert-Exit in tools.ps1
. (Join-Path $PSScriptRoot 'tools.ps1')

$Py = Get-LocalPython
Install-PythonPackage $Py 'ziglang' 'ziglang'

# ArduinoJson: same version as the firmware (single-header release)
$AjVersion = '7.4.3'
$SimDir = Join-Path $ToolsDir 'sim'
$Deps = Join-Path $SimDir "deps\ArduinoJson-$AjVersion"
if (-not (Test-Path (Join-Path $Deps 'ArduinoJson.h'))) {
    Write-Host "==> Downloading ArduinoJson $AjVersion into .tools\sim"
    New-Item -ItemType Directory -Force $Deps | Out-Null
    Invoke-WebRequest -UseBasicParsing -ErrorAction Stop -OutFile (Join-Path $Deps 'ArduinoJson.h') `
        -Uri "https://github.com/bblanchon/ArduinoJson/releases/download/v$AjVersion/ArduinoJson-v$AjVersion.h"
}

# Compile to WebAssembly (only when a source changed)
$Wasm = Join-Path $SimDir "$Variant.wasm"
$sources = @(Get-ChildItem (Join-Path $Root 'app\idfm') -Filter *.cpp) +
           @(Get-Item (Join-Path $Root 'sim\sim_core.cpp'), (Join-Path $Root 'sim\sim_wasm.cpp'))
$inputs  = $sources + @(Get-ChildItem (Join-Path $Root 'app\idfm') -Filter *.h) +
           @(Get-ChildItem (Join-Path $Root 'sim') -Recurse -File -Include *.h, *.c, *.cpp) + @(Get-Item $PSCommandPath)
$newest  = ($inputs | Measure-Object LastWriteTime -Maximum).Maximum
if (-not (Test-Path $Wasm) -or (Get-Item $Wasm).LastWriteTime -lt $newest) {
    Write-Host '==> Compiling simulator (WebAssembly)'
    $zigArgs = @(
        '-m', 'ziglang', 'c++', '-std=gnu++17', '-O2', '-g0', '-target', 'wasm32-wasi', '-fno-exceptions',
        '-I', (Join-Path $Root 'sim\shim'), '-I', $Deps,
        '-DIDFM_SIMULATOR=1', '-D_POSIX_THREAD_SAFE_FUNCTIONS=1',
        '-DARDUINOJSON_ENABLE_ARDUINO_STRING=1', '-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0',
        '-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0', '-DARDUINOJSON_ENABLE_PROGMEM=0',
        '-Wno-unused-function', '-Wno-format-security',
        '-Wl,-z,stack-size=1048576'
    ) + ($sources | ForEach-Object { $_.FullName }) + @('-o', $Wasm)
    & $Py @zigArgs 2>&1 | ForEach-Object { "$_" }
    Assert-Exit 'Simulator compilation'
}
if ($NoRun) { Write-Host "==> Built $Wasm"; exit 0 }

# Page parameters
$query = @()
if ($Demo)      { $query += "demo=$Board" }
if ($Offline)   { $query += 'offline=1' }
if ($Keys)      { $query += 'keys=' + ($Keys -replace '[^npsew]', '') }
if ($ExitAfter) { $query += "exit=$ExitAfter" }
if ($Shot)      { $query += "shot=$Shot"; $query += 'quit=1' }
$qs = if ($query) { '?' + ($query -join '&') } else { '' }

$server = Join-Path $Root 'sim\server.py'
if (-not $Shot) {
    # Interactive: serve and open the default browser (Ctrl+C to stop)
    $srvArgs = @($server, '--wasm', $Wasm, '--port', $Port, '--open', $qs)
    if ($NoBrowser) { $srvArgs += '--no-browser' }
    & $Py @srvArgs
    exit $LASTEXITCODE
}

# Headless capture with Microsoft Edge (already part of Windows): the page posts the PNG to the
# local server, which stops once it is saved.
if (-not $ExitAfter) { throw '-Shot needs -ExitAfter <ms>' }
# Own port so a capture never collides with an interactive session on the default port
if (-not $PSBoundParameters.ContainsKey('Port')) { $Port = 8799 }
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
          "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge not found (needed for -Shot)' }
$srv = Start-Process -PassThru -NoNewWindow -FilePath $Py -ArgumentList @(
    "`"$server`"", '--wasm', "`"$Wasm`"", '--port', $Port, '--no-browser', '--quit-after-shot')
Start-Sleep -Milliseconds 800
$profileDir = Join-Path $SimDir "edge-profile-$Port" # one per port: parallel captures don't collide
$browser = Start-Process -PassThru -FilePath $edge -ArgumentList @(
    '--headless=new', '--disable-gpu', '--no-first-run', "--user-data-dir=`"$profileDir`"",
    '--window-size=1100,900', "`"http://127.0.0.1:$Port/$qs`"")
$ok = $srv.WaitForExit($ExitAfter + 30000)
Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" |
    Where-Object { $_.CommandLine -like "*$profileDir*" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
if (-not $ok) { Stop-Process -Id $srv.Id -Force; throw 'Capture timed out' }
Write-Host "==> Capture: $(Join-Path $SimDir "shots\$Shot")"
