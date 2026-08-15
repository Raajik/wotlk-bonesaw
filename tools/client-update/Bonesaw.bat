@echo off
setlocal
set HERE=%~dp0
if exist "%HERE%Wow.exe" (
  set CLIENT=%HERE%
) else (
  set CLIENT=B:\Games\WoW 3.3.5\Bonesaw
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%BonesawLauncher.ps1"
if errorlevel 1 (
  echo Launcher failed. Starting Wow.exe without an update check.
  start "" /D "%CLIENT%" "%CLIENT%Wow.exe"
)
