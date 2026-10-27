# Builds G940FFBCheck.exe with Visual Studio Build Tools (x64). Run from any shell.
$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$src = Join-Path $PSScriptRoot 'G940FFBCheck.cpp'
$out = Join-Path $PSScriptRoot 'G940FFBCheck.exe'
cmd /c "`"$vcvars`" >nul && cl /nologo /EHsc /MT /O2 /W4 /std:c++17 `"$src`" /Fe:`"$out`" /Fo:`"$env:TEMP\\`" dinput8.lib dxguid.lib user32.lib"
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Write-Host "Built $out"
