@echo off
setlocal
set HERE=%~dp0
if exist "%HERE%Wow.exe" (
  set CLIENT=%HERE%
) else (
  set CLIENT=B:\Games\WoW 3.3.5\Bonesaw
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%BonesawPatchExe.ps1"
if errorlevel 1 (
  echo Patch failed. Close Wow and use a stock 3.3.5a 12340 Wow.exe.
  pause
  exit /b 1
)
pause
