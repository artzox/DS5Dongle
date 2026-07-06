@echo off
REM ds5-setup.bat - double-click to run the DS5Dongle automation installer.
REM It generates all scripts with the correct paths for THIS folder.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ds5-setup.ps1"
