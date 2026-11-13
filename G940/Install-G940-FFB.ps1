<#
Logitech G940 force feedback for Windows 10/11 WITHOUT kernel drivers.

The G940 works as a plain HID joystick on the in-box Windows HID driver, so
input needs nothing. Force feedback only needs DirectInput to know which
effect driver to load. Logitech's own (Logitech-signed, user-mode) effect
driver wmjoyfrc.dll / wmjoyf32.dll already contains a G940 implementation
behind CLSID {8D533A50-7A5F-11D3-8297-0050DA1A72D3}; it talks to the device
through the normal HID device path (CreateFile/WriteFile/HidD_SetFeature),
so no filter driver, no test signing and no change to Memory Integrity
(Core Isolation) is needed.

This script copies the two DLLs to Program Files and writes the registry
entries the original Logitech package used. Run it as Administrator.
    .\Install-G940-FFB.ps1            install
    .\Install-G940-FFB.ps1 -Uninstall remove everything this script added
#>
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'

$Clsid      = '{8D533A50-7A5F-11D3-8297-0050DA1A72D3}'
$OemKey     = 'HKLM:\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C287'
$InstallDir = Join-Path $env:ProgramFiles 'G940 Force Feedback'
$Clsid64    = "HKLM:\SOFTWARE\Classes\CLSID\$Clsid"
$Clsid32    = "HKLM:\SOFTWARE\Classes\WOW6432Node\CLSID\$Clsid"
$Marker     = 'InstalledBy'
$MarkerText = 'Install-G940-FFB.ps1'

# Logitech-signed binaries from the repository root, pinned by hash.
$Dlls = @(
    @{ Name = 'wmjoyfrc.dll'; Sha256 = 'ADF48CCB99127A41DDEA7B58639F873C99F94ADDB5AC9FD09E2EC808F6CDCE79' },
    @{ Name = 'wmjoyf32.dll'; Sha256 = '7F44A6E74A90859C94451E06D0310F3CAC317D2B93B44E5E8F0E5B3CADB0E08F' }
)

function Assert-Admin {
    $p = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this script as Administrator.'
    }
}

function Remove-KeyIfOurs([string]$Path) {
    if ((Test-Path $Path) -and ((Get-ItemProperty $Path -ErrorAction SilentlyContinue).$Marker -eq $MarkerText)) {
        Remove-Item $Path -Recurse -Force
        Write-Host "Removed $Path"
    }
}

# New-Item -Force on an existing registry key wipes its values, so only create missing keys.
function Ensure-Key([string]$Path) {
    if (-not (Test-Path $Path)) { New-Item -Path $Path -Force | Out-Null }
}

function Set-Binary([string]$Path, [string]$Name, [byte[]]$Bytes) {
    Ensure-Key $Path
    New-ItemProperty -Path $Path -Name $Name -PropertyType Binary -Value $Bytes -Force | Out-Null
}

function Set-Default([string]$Path, [string]$Value) {
    Ensure-Key $Path
    Set-ItemProperty -Path $Path -Name '(default)' -Value $Value
}

Assert-Admin

if ($Uninstall) {
    Remove-KeyIfOurs $Clsid64
    Remove-KeyIfOurs $Clsid32
    if (Test-Path "$OemKey\OEMForceFeedback") {
        if ((Get-ItemProperty "$OemKey\OEMForceFeedback").$Marker -eq $MarkerText) {
            Remove-Item "$OemKey\OEMForceFeedback" -Recurse -Force
            foreach ($axis in 0, 1) { Remove-Item "$OemKey\Axes\$axis" -Recurse -Force -ErrorAction SilentlyContinue }
            if ((Test-Path "$OemKey\Axes") -and -not (Get-ChildItem "$OemKey\Axes")) { Remove-Item "$OemKey\Axes" -Force }
            Write-Host "Removed force feedback entries under $OemKey"
        }
    }
    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force; Write-Host "Removed $InstallDir" }
    Write-Host 'Uninstall complete. Replug the G940 or restart games to apply.'
    return
}

# 1. Verify and copy the Logitech DLLs. A standalone package keeps them next to
#    this script; in the repository they live one folder up.
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
foreach ($dll in $Dlls) {
    $src = @((Join-Path $PSScriptRoot $dll.Name), (Join-Path (Split-Path $PSScriptRoot -Parent) $dll.Name)) |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $src) { throw "Missing $($dll.Name) (looked next to this script and one folder up)" }
    $hash = (Get-FileHash $src -Algorithm SHA256).Hash
    if ($hash -ne $dll.Sha256) { throw "$($dll.Name) hash mismatch ($hash). Refusing to install a modified DLL." }
    $sig = Get-AuthenticodeSignature $src
    if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Logitech') {
        throw "$($dll.Name) is not validly signed by Logitech ($($sig.Status))."
    }
    Copy-Item $src $InstallDir -Force
}
$dll64 = Join-Path $InstallDir 'wmjoyfrc.dll'
$dll32 = Join-Path $InstallDir 'wmjoyf32.dll'

# 2. COM registration for 64-bit and 32-bit games.
foreach ($entry in @(@{ Key = $Clsid64; Dll = $dll64 }, @{ Key = $Clsid32; Dll = $dll32 })) {
    Set-Default $entry.Key 'Logitech G940 FF DLL'
    New-ItemProperty -Path $entry.Key -Name $Marker -Value $MarkerText -Force | Out-Null
    Set-Default "$($entry.Key)\InProcServer32" $entry.Dll
    New-ItemProperty -Path "$($entry.Key)\InProcServer32" -Name 'ThreadingModel' -Value 'Both' -Force | Out-Null
}

# 3. DirectInput OEM entries (same layout the original Logitech INF uses for
#    its force feedback joysticks). Main stick X/Y = DirectInput axes 0/1.
Ensure-Key $OemKey
if (-not (Get-ItemProperty $OemKey -ErrorAction SilentlyContinue).OEMName) {
    New-ItemProperty -Path $OemKey -Name 'OEMName' -Value 'Logitech G940' -Force | Out-Null
}
foreach ($axis in @(@{ N = 0; Name = 'X Axis' }, @{ N = 1; Name = 'Y Axis' })) {
    $k = "$OemKey\Axes\$($axis.N)"
    Set-Default $k $axis.Name
    # DIOBJECTATTRIBUTES.dwFlags = DIDOI_FFACTUATOR | DIDOI_ASPECTPOSITION | DIDOI_POLLED
    Set-Binary $k 'Attributes'   ([byte[]](0x01, 0x81, 0x00, 0x00))
    # DIFFOBJECTATTRIBUTES: max force 10, resolution 256
    Set-Binary $k 'FFAttributes' ([byte[]](0x0A, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00))
}

$ff = "$OemKey\OEMForceFeedback"
Ensure-Key $ff
New-ItemProperty -Path $ff -Name 'CLSID' -Value $Clsid -Force | Out-Null
New-ItemProperty -Path $ff -Name $Marker -Value $MarkerText -Force | Out-Null
# DIFFDEVICEATTRIBUTES: flags 0, sample period 1000 us, min time resolution 1000 us
Set-Binary $ff 'Attributes' ([byte[]](0, 0, 0, 0, 0xE8, 3, 0, 0, 0xE8, 3, 0, 0))

# DIEFFECTATTRIBUTES: effect id, type, static params, dynamic params, coords
$periodic = 0x86, 0, 0, 0xEF, 3, 0, 0, 0xEF, 3, 0, 0, 0x30, 0, 0, 0
$condition = 0xD8, 0, 0, 0x6D, 3, 0, 0, 0x6D, 3, 0, 0, 0x30, 0, 0, 0
$effects = @(
    @{ Guid = '{13541C20-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Constant';      Id = 0x000; Type = 1; Rest = 0x86, 0, 0, 0xED, 3, 0, 0, 0xED, 3, 0, 0, 0x30, 0, 0, 0 },
    @{ Guid = '{13541C21-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Ramp Force';    Id = 0x001; Type = 2; Rest = $periodic },
    @{ Guid = '{13541C22-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Square';        Id = 0x002; Type = 3; Rest = $periodic },
    @{ Guid = '{13541C23-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Sine';          Id = 0x003; Type = 3; Rest = $periodic },
    @{ Guid = '{13541C24-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Triangle';      Id = 0x004; Type = 3; Rest = $periodic },
    @{ Guid = '{13541C25-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Sawtooth Up';   Id = 0x005; Type = 3; Rest = $periodic },
    @{ Guid = '{13541C26-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Sawtooth Down'; Id = 0x006; Type = 3; Rest = $periodic },
    @{ Guid = '{13541C27-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Spring';        Id = 0x007; Type = 4; Rest = $condition },
    @{ Guid = '{13541C28-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Damper';        Id = 0x008; Type = 4; Rest = $condition },
    @{ Guid = '{13541C29-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Inertia';       Id = 0x009; Type = 4; Rest = $condition },
    @{ Guid = '{13541C2A-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Friction';      Id = 0x00A; Type = 4; Rest = $condition },
    @{ Guid = '{13541C2B-8E33-11D0-9AD0-00A0C9A06E35}'; Name = 'Custom Force';  Id = 0x100; Type = 5; Rest = $periodic }
)
foreach ($e in $effects) {
    $k = "$ff\Effects\$($e.Guid)"
    Set-Default $k $e.Name
    $bytes = [byte[]](@([BitConverter]::GetBytes([uint32]$e.Id)) + @([byte]$e.Type) + @($e.Rest))
    Set-Binary $k 'Attributes' $bytes
}

Write-Host ''
Write-Host "Installed G940 force feedback (user-mode only) to $InstallDir"
Write-Host 'Unplug and replug the G940, then run G940FFBCheck.exe to confirm DirectInput reports force feedback.'
