# m365-inverter

Turn a spare **Xiaomi M365 ESC** (motor-controller board, STM32F103C8) into a
**single-phase 50 Hz sine inverter**. The ESC's 3-phase MOSFET bridge is driven
as an H-bridge across two legs, producing center-aligned SPWM that feeds a
step-up transformer.

> ⚠️ **DANGER — LETHAL VOLTAGE.** With a step-up transformer the output is
> mains-level AC (≈230 V) with **no isolation guarantees**. Build and test only
> on a bench, behind a fuse, with the secondary never touched while live. You are
> responsible for your own safety. This is an experimental hobby project provided
> with **no warranty**.

---

## How it works

- **TIM1** runs center-aligned complementary PWM, `ARR = 2028`, clock 64 MHz →
  **f_pwm ≈ 15.8 kHz**.
- Legs **A (CH1)** and **B (CH2)** form the H-bridge; **leg C (CH3) is parked**
  at 50 % and unused. A DDS phase accumulator + `arm_sin_q31` generate a 50 Hz
  sine: `CCR1 = 1014 + val`, `CCR2 = 1014 − val`.
- The control loop runs from the **ADC injected end-of-conversion** interrupt
  (triggered by TIM1_CC4), once per PWM period.
- Phase currents (low-side shunts on ADC1/ADC2, injected, offset-calibrated at
  boot) give **fast latched over-current protection**.
- A 10 ms slow loop reads supply voltage + temperature and does **feed-forward
  output-voltage regulation** (holds the target RMS as the supply sags) and
  **thermal fold-back**.

## Hardware wiring

```
   DC supply (e.g. 20–42 V)
        │  (fuse!)
        ▼
   ┌──────────┐   leg A ─┐
   │  M365     │          ├─[ series inductor ]─┬─ transformer primary (LV, e.g. 12 V)
   │  ESC      │   leg B ─┘                     │
   └──────────┘                          [ film cap ]  ← LC output filter
        ▲                                       │
     ST-Link (SWD)                    transformer secondary (HV) → load
```

- **Legs A/B** = two of the three fat motor-phase wires (phase A, phase B).
- A **series inductor** on the primary + a **film cap** across the output form
  the LC filter that reconstructs the sine and kills the switching ripple —
  without it the primary wiring heats up from carrier current.
- Leg C (third phase wire) is left unconnected.

## Protection

| Protection | Type | Behaviour |
|---|---|---|
| Over-current / short | fast, per-PWM-cycle | **latched** — output off, LED solid, until power-cycle |
| Over-temperature | 10 ms loop | **fold-back** — ramps output to 0, auto-recovers on cooldown |
| Soft-start | always | amplitude slews up gradually on power-up |

Thresholds live in [`Core/Inc/inverter.h`](Core/Inc/inverter.h).

## Configuration ([`Core/Inc/inverter.h`](Core/Inc/inverter.h))

| Define | Meaning |
|---|---|
| `INV_OUT_HZ` | output frequency (50) |
| `INV_VOUT_TARGET` | regulated secondary RMS, volts |
| `INV_CAL_AMP / INV_CAL_VOUT / INV_CAL_VBAT_MV` | one measured calibration point |
| `INV_AMP_MAX` | hard amplitude ceiling (safety) |
| `INV_OC_TRIP_A` | over-current trip (phase amps) |
| `INV_TEMP_LIMIT_C / INV_TEMP_CLEAR_C` | thermal fold-back thresholds |

**Calibrate** by measuring the secondary RMS at a known amplitude and supply,
then set `INV_CAL_*` to those numbers. Output is linear in amplitude.

## Build

Requires `gcc-arm-none-eabi` + `make`.

```sh
make
# -> build/firmware.bin  (linked at 0x08000000, standalone, no bootloader)
```

## Flash (ST-Link + OpenOCD)

The stock M365 ESC ships **read-protected (RDP level 1)**; removing protection
mass-erases the chip (you cannot back up the stock image first).

```sh
# one-time: remove RDP (ERASES the chip)
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f1x.cfg \
  -c "init; reset halt; stm32f1x unlock 0; reset halt; exit"

# flash
openocd -f interface/stlink.cfg -c "transport select hla_swd" -f target/stm32f1x.cfg \
  -c "init; reset halt; flash write_image erase build/firmware.bin 0x08000000; reset run; exit"
```

To restore the scooter, flash a stock M365 DRV image (from scooterhacking.org) at
`0x08000000` the same way.

## Telemetry

USART1 TX on **PB6**, 115200 8N1, prints once per 0.5 s:

```
Vbat=32.10V  Vout~230V  amp=367  T=28C  fault=0
```

The status **LED (PD1)** heartbeats normally and goes solid on a latched fault.

## Status

⚠️ The current-sensing path was rewritten for the inverter use-case (fixed
phase-A/B shunt read, no rotor/hall logic). **Validate on the bench** (scope the
output, confirm the over-current trip) before trusting it with a real load.

## Credits & license

Peripheral bring-up (clock, TIM1, ADC, GPIO) is derived from the open-source
motor firmware:

- **[EBiCS_motor_FOC](https://github.com/EBiCS/EBiCS_motor_FOC)**
- **[Koxx3/SmartESC_STM32_v2](https://github.com/Koxx3/SmartESC_STM32_v2)** /
  **[martyd420/SmartESC_STM32_v3](https://github.com/martyd420/SmartESC_STM32_v3)**

STM32 HAL is © STMicroelectronics (BSD-3-Clause); CMSIS is © ARM (Apache-2.0).

This project is licensed under the **GNU GPL v3** — see [LICENSE](LICENSE).
