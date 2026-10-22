@echo off
rem Witcher 3 Remastered VR - Windows uninstaller. Your saves are not touched.
title Uninstall Witcher 3 Remastered VR
rem One line: the uninstaller may delete this file (the copy in the game folder) while it runs.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\uninstall.ps1" & echo. & pause & exit /b
