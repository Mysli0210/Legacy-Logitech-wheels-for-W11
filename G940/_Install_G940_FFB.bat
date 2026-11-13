@echo off
:: Installs G940 force feedback without kernel drivers (Core Isolation can stay on).
:: Double-click it; it asks for administrator rights itself.
net session >nul 2>&1
if %errorlevel% neq 0 (
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-G940-FFB.ps1"
pause
