@echo off
rem Witcher 3 Remastered VR - Windows installer. Everything comes from this folder; nothing is downloaded.
title Install Witcher 3 Remastered VR
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\install.ps1"
echo.
pause
