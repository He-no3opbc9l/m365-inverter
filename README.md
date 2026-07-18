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

"Electronic breaker" model: the output holds **full voltage** — overload is caught
by tripping, not by throttling (a drooping output makes constant-power/SMPS loads
draw *more* current and collapse).

| Protection | Acts on | Behaviour |
|---|---|---|
| Short circuit | instantaneous peak, per-PWM-cycle (debounced) | **latched** — output off, LED solid, until power-cycle |
| Overload | **averaged** current (~4 ms low-pass), inverse-time integral | **latched** — trips sooner the bigger the overload |
| Over-temperature | 10 ms loop | **fold-back** — ramps output to 0, auto-recovers on cooldown |
| Soft-start | always | amplitude ramps up gradually (~5 s) on power-up |

Averaging matters: capacitive/SMPS/triac loads draw brief high peaks. Limiting on
the *peak* throttled them; the average lets real power through and only sustained
overload trips. Approx overload trip times at the default settings: ~1.2 s @25 A,
~0.35 s @30 A, ~0.15 s @40 A (phase current).

Thresholds live in [`Core/Inc/inverter.h`](Core/Inc/inverter.h).

## Configuration ([`Core/Inc/inverter.h`](Core/Inc/inverter.h))

| Define | Meaning |
|---|---|
| `INV_OUT_HZ` | output frequency (50) |
| `INV_AMP_SET` | **fixed open-loop amplitude** — tune for your output voltage |
| `INV_REGULATE` | 0 = open loop (default); 1 = feed-forward regulation from Vbat |
| `INV_RAMP_DIV` | soft-start ramp rate (~5 s to full) |
| `INV_ILIM_A` | average current considered overload (phase amps) |
| `INV_OL_TRIP` | inverse-time integral threshold — lower = trips sooner |
| `INV_OC_TRIP_A` | instantaneous short-circuit latch (phase amps) |
| `INV_TEMP_ENABLE`, `INV_TEMP_LIMIT_C / INV_TEMP_CLEAR_C` | thermal fold-back |

**Calibrate the output** by raising `INV_AMP_SET` until the secondary reads the
wanted RMS — output is linear in amplitude. Reference: ~350 gave ~225 V at ~30 V
supply on the test transformer.

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

Working on hardware: clean 50 Hz sine, ~225 V from a ~30 V supply, and it drives
real loads (SMPS phone charger / TV) without false trips.

## TODO / known limitations

- **Output voltage regulation.** Currently open loop (fixed amplitude), so the
  output moves with the supply voltage and sags under load. Vbat is now read
  reliably, so feed-forward (`INV_REGULATE 1`) can be re-enabled — but true load
  regulation needs secondary-side voltage sensing, which the board does not have.
- **⚠️ Protection is NOT validated against device damage.** The overload and
  short-circuit thresholds/timings have not been proven to trip *before* the
  MOSFETs are damaged. Treat the current settings as provisional.
- **Short-circuit behaviour depends on the supply.** With a current-limited bench
  PSU the PSU folds back before the firmware's instantaneous latch is reached, so
  the PSU is what actually protects. Validate the firmware thresholds on a supply
  that can deliver fault current.
- **Hot-plugging capacitive loads** still causes a large inrush spike (measured
  ~65 A). Connect the load *before* powering the inverter so the soft-start charges
  it gently, or add hardware inrush limiting (NTC/resistor).
- **Temperature calibration is rough** — two close bench points (~30 °C→1790,
  ~40 °C→1590 counts) extrapolated linearly. Re-calibrate at a real hot point.
- No output filtering beyond the external LC; a series inductor + film cap on the
  transformer are required (see wiring) or the switching carrier dominates.
- Debugging note: an ST-Link `halt` freezes the sine and puts DC on the
  transformer — read telemetry over UART instead while the output is live.

## Credits & license

Peripheral bring-up (clock, TIM1, ADC, GPIO) is derived from the open-source
motor firmware:

- **[EBiCS_motor_FOC](https://github.com/EBiCS/EBiCS_motor_FOC)**
- **[Koxx3/SmartESC_STM32_v2](https://github.com/Koxx3/SmartESC_STM32_v2)** /
  **[martyd420/SmartESC_STM32_v3](https://github.com/martyd420/SmartESC_STM32_v3)**

STM32 HAL is © STMicroelectronics (BSD-3-Clause); CMSIS is © ARM (Apache-2.0).

This project is licensed under the **GNU GPL v3** — see [LICENSE](LICENSE).
