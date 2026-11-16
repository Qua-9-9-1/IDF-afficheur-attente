@echo off
rem Runs scripts\sim.ps1 without changing the machine's PowerShell execution policy
rem (Windows default 'Restricted' blocks .ps1 files; -ExecutionPolicy applies to this run only).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\sim.ps1" %*
exit /b %ERRORLEVEL%
