@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start_BC1.ps1"
if errorlevel 1 pause
