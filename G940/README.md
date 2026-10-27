# Logitech G940 force feedback on Windows 10/11 - no kernel driver

Works with **Memory Integrity / Core Isolation ON** and no test signing.

## Why this works

The G940 already works as a normal HID joystick on Windows' own driver: all
axes, buttons and hats show up (as one combined device). What's missing is
force feedback, and that's a user-mode problem, not a kernel one:

- DirectInput loads a force feedback "effect driver" DLL for a joystick when
  the registry key
  `HKLM\SYSTEM\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM\VID_046D&PID_C287\OEMForceFeedback`
  names one.
- Logitech's own effect driver, `wmjoyfrc.dll` (64-bit) and `wmjoyf32.dll`
  (32-bit) in the repository root, is signed by Logitech and already contains a
  G940 implementation behind CLSID `{8D533A50-7A5F-11D3-8297-0050DA1A72D3}`.
  The `wmjoyhid.inf` in this repository never registers that CLSID.
- Disassembly shows the DLL needs no Logitech kernel driver. Its G940
  `DeviceID` builds the G940 device object directly. The DLL finds the device
  through DirectInput, then talks to it with plain `CreateFile` / `WriteFile`
  (output report 02) and `HidD_SetFeature`. It imports no `DeviceIoControl`,
  so it doesn't depend on `wmhidlo.sys` / `wmfilter.sys`.

So the installer just copies the two signed DLLs and writes the registry
entries. The original Logitech package used the same entry layout for its
force feedback joysticks: the FF device key, X/Y as FF actuators, and constant,
ramp, periodic, spring, damper, inertia, friction and custom effects.

## Install

1. **Don't** install the kernel-driver package from the repository root for the
   G940. It puts `WmHidLo`/`WmFilter` on the G940, and those need Core
   Isolation off. If you already installed it, run `_Uninstall.bat` first and
   check that the G940 is back on the standard "HID-compliant game controller"
   driver.
2. Double-click `_Install_G940_FFB.bat` and click **Yes** when Windows asks
   for administrator rights.
3. Unplug the G940 and plug it back in. Restart any game that was open.
4. Run `G940FFBCheck.exe` (in PowerShell: `.\G940FFBCheck.exe`). It's read-only and sends nothing to the
   stick. It should print `force feedback: YES`, list the X/Y FF axes and list
   the effects.
5. Optional hands-on test: `G940FFBCheck.exe --spring` asks for confirmation,
   then runs a moderate centering spring until you press Enter. Keep a hand
   on the stick and the motor power switch within reach.

Uninstall: double-click `_Uninstall_G940_FFB.bat`. It removes only what the
installer added.

## Notes

- Input stays as one combined device (stick, throttle and pedals together),
  exactly as it is without this add-on. Splitting it into three devices was a
  job for the old kernel filter and isn't part of this.
- The firmware only applies host forces while a peripheral/grip status bit is
  set, which is most likely the palm sensor on the grip. If you don't feel
  any force, make sure your hand is on the grip.
- The firmware adds a fixed offset to any non-zero force, so even small
  game forces feel noticeably firm. The original Logitech driver behaved
  the same way on this firmware (FW 0142).
- Status: confirmed working on a G940 (FW 0142) under Windows 11 with Memory
  Integrity on. `G940FFBCheck.exe --suite` started spring, damper, friction,
  constant and sine effects without error, and the user felt them. The logged
  sine steps oscillate at the requested 4 Hz on the commanded axis. The
  direction of constant forces wasn't measured because the grip was held
  firmly. The 32-bit DLL (`wmjoyf32.dll`) is registered the same way, but no
  32-bit game has been tried yet.
- `G940FFBCheck.exe --suite` runs every effect in one ~42 s timed sequence
  (Esc/Ctrl+C stops all forces) and logs stick position to a CSV.
  `--plan` validates the effects without taking control of the stick.
