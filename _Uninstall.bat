@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo ============================================
echo Mysli Driver Uninstaller
echo ============================================
echo.

:: Ensure running as admin
net session >nul 2>&1
if %errorlevel% neq 0 (
echo ERROR: This script must be run as Administrator.
pause
exit /b 1
)

set CERT_SUBJECT=Mysli's driver certificate
set INF_NAME=wmjoyhid.inf

echo Removing installed driver package...

:: Find published driver name (oemXX.inf)
for /f "tokens=1,2 delims=:" %%A in ('pnputil /enum-drivers ^| findstr /i "%INF_NAME%"') do (
for /f "tokens=* delims= " %%X in ("%%B") do set OEM=%%X
)

if defined OEM (
pnputil /delete-driver !OEM! /uninstall /force
) else (
echo Driver package not found.
)

echo.
echo Removing certificate from Trusted Root...
certutil -delstore Root "%CERT_SUBJECT%"

echo Removing certificate from Trusted Publishers...
certutil -delstore TrustedPublisher "%CERT_SUBJECT%"

echo.
echo ============================================
echo Uninstall complete.
echo ============================================
pause
exit /b 0