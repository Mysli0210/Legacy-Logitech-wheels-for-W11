@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo ============================================
echo  Mysli Driver Installer
echo ============================================
echo.

:: Ensure running as admin
net session >nul 2>&1
if %errorlevel% neq 0 (
echo ERROR: This script must be run as Administrator.
pause
exit /b 1
)

set CERT_FILE=%~dp0Myslisdrivercert.cer
set INF_FILE=%~dp0wmjoyhid.inf

if not exist "%CERT_FILE%" (
echo ERROR: Certificate file not found.
pause
exit /b 1
)

if not exist "%INF_FILE%" (
echo ERROR: INF file not found.
pause
exit /b 1
)

echo Installing certificate to Trusted Root...
certutil -addstore -f Root "%CERT_FILE%"
if %errorlevel% neq 0 goto :error

echo Installing certificate to Trusted Publishers...
certutil -addstore -f TrustedPublisher "%CERT_FILE%"
if %errorlevel% neq 0 goto :error

echo.
echo Installing driver package...
pnputil /add-driver "%INF_FILE%" /install
if %errorlevel% neq 0 goto :error

echo.
echo ============================================
echo Installation complete.
echo ============================================
echo
echo
echo
echo
echo ============================================
echo If driver fails to load, ensure:
echo  - Test mode is enabled
echo To enable test mode either, pressing restart in the start menu,
echo whilst holding shift.
echo when restarting, the pc will let you run in a manner of ways,
echo one of them is to run with Driver signature Enforcement off.
echo 
echo Or in a terminal/powershell/cmd run 
echo bcdedit /set TESTSIGNING OFF
echo and restart
echo ============================================
pause
exit /b 0

:error
echo.
echo Installation failed.
pause
exit /b 1
