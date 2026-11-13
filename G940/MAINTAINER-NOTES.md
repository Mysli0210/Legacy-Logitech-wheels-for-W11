# G940 force feedback: notes for the Legacy-Logitech-wheels-for-W11 maintainer

This is a contribution for the Logitech G940 flight system (VID 046D, PID C287).
It's designed to drop into the repository as a `G940/` folder. It doesn't touch
any existing file.

## The short version

The G940 gets full force feedback on Windows 10/11 **without any kernel driver**.
That means no test signing, no self-signed certificate, and Memory Integrity /
Core Isolation can stay on. It reuses the Logitech-signed `wmjoyfrc.dll` /
`wmjoyf32.dll` that are already in the repo, and adds only registry entries.

It was tested on a real G940 (FW 0142) under Windows 11 with Memory
Integrity on. Spring, damper, friction, constant and periodic effects all ran.

## Why it works

- The G940 runs fine on the in-box `HidUsb` / `input.inf` driver. All axes,
  buttons and hats show up as one combined game controller.
- `wmjoyfrc.dll` exposes CLSID `{8D533A50-7A5F-11D3-8297-0050DA1A72D3}`. It's
  entry 17 in the `DllGetClassObject` table at RVA 0x17F30. Its
  `IDirectInputEffectDriver::DeviceID` (RVA 0x80C0) always constructs the
  G940 device object (constructor RVA 0x135D0, configuration ID 0x046DC287).
  The current `wmjoyhid.inf` never registers this CLSID.
- The DLL reaches the device through `DirectInputCreateW`, then `CreateFileW` /
  `WriteFile` (64-byte output report 02) and `HidD_SetFeature`. It imports no
  `DeviceIoControl`, so it doesn't need `wmhidlo.sys` / `wmfilter.sys`.
- DirectInput loads it once
  `HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C287\OEMForceFeedback`
  has `CLSID` = the GUID above. The entries use the same layout as the INF's
  Force 3D joystick section: FF attributes, axes 0/1 as FF actuators, and the
  standard 12 effects.

## Files

| File | Purpose |
|---|---|
| `Install-G940-FFB.ps1` | Checks the DLL hashes and Logitech signatures, copies the DLLs to `Program Files\G940 Force Feedback`, writes the COM and DirectInput registry entries. `-Uninstall` removes only what it added. |
| `_Install_G940_FFB.bat` / `_Uninstall_G940_FFB.bat` | Double-click wrappers that ask for administrator rights themselves. |
| `G940FFBCheck.exe` / `.cpp` | DirectInput checker. With no arguments it's read-only. `--plan` validates the effects without acquiring the device. `--suite` runs a ~42 s timed test of every effect type and logs stick position to CSV. `--spring` runs a simple centering spring. |
| `Build-G940FFBCheck.ps1` | Rebuilds the checker with Visual Studio C++ build tools. |
| `README.md` | User instructions. |

## Things to know

- **Conflict with the kernel package:** the current `wmjoyhid.inf` binds
  `USB\VID_046D&PID_C287` to `MAZInst` (WmHidLo filter) and the HID IDs to
  `WmJoy.HidFilter`. On a machine with Memory Integrity on, those filters are
  what break the G940. Consider removing the C287 / C2A8 / C2A9 / C2AA lines
  from the INF (the catalog would need re-signing), or tell G940 users to use
  only the `G940/` folder.
- **Input layout:** input stays as one combined device. The old WmFilter split
  it into joystick / throttle / pedals children. Recreating that split isn't
  part of this contribution.
- **Possibly useful for other wheels:** the same approach (in-box HID driver
  plus the Logitech DLL's CLSID in `OEMForceFeedback`) might work for other
  Logitech devices whose FF path is also in user mode. Each device needs
  checking, because wheels also rely on the filter for mode switching and
  range.
