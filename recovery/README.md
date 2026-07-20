# Recovery — putting the scooter back to stock

Use this to return an ESC to stock Xiaomi firmware after running the inverter
firmware, or to revive one that no longer boots.

> **Use ScooterHacking ReFlasher.** It writes a *matched* set of bootloader +
> application + data block. Hand-flashing the pieces yourself can produce an ESC
> that boots and rides fine but whose **OTA updating is broken** — that failure was
> observed while developing this project (the scooter froze whenever an update was
> attempted, and only unplugging the battery recovered it). ReFlasher fixed it.

## What you need

Download from the official sources (nothing is redistributed here — these are
third-party tools with their own licences):

| Tool | Where |
|---|---|
| **ScooterHacking ReFlasher** | <https://www.scooterhacking.org/forum/viewtopic.php?t=676> — see also the [wiki page](https://joeybabcock.me/wiki/ScooterHacking_ReFlasher) |
| **.NET Desktop Runtime 3.1 (x64)** — ReFlasher dependency | <https://dotnet.microsoft.com/download/dotnet/3.1> |
| **ST-Link USB driver (STSW-LINK009)** | <https://www.st.com/en/development-tools/stsw-link009.html> |

Plus an **ST-Link V2** dongle (a clone is fine).

Tested combination: ReFlasher 1.4.2, .NET Desktop Runtime 3.1.32 x64,
STSW-LINK009, ST-Link V2 clone (firmware V2J40S7).

## Wiring

The SWD pads are on the DRV board — either remove it from the heatsink to reach
the pads underneath, or peel back the black silicone to reach them from the top.
**Discharge the capacitor first.**

| ST-Link | ESC pad |
|---|---|
| SWCLK | `C` |
| SWDIO | `D` |
| GND | `G` |
| 5.0 V | pin `5` of the 4-pin BLE header |

## Procedure

1. Install the .NET runtime, the ST-Link driver, then ReFlasher.
2. Connect the ST-Link as above and power the ESC.
3. Run ReFlasher, pick your scooter model and the firmware version, and flash.
4. **Power-cycle for real** (unplug the battery) when it finishes — a warm reset
   does not reliably hand control from the bootloader to the application.

## Notes

- Stock firmware **re-enables read protection (RDP)** by itself. To flash this
  project's inverter firmware again afterwards you must unlock first, which
  **mass-erases** the chip (that is what destroys the factory bootloader, which is
  why recovery needs ReFlasher rather than just a stock `.bin`).
- Flash layout, for reference: bootloader at `0x08000000`, DRV application at
  `0x08001000`, data block (serial / odometer / **MCU UUID**) at `0x0800f800`.
  The data block is tied to the chip's UUID (`0x1FFFF7E8`), so it cannot simply be
  copied between boards.
