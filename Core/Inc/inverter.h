#ifndef INVERTER_H
#define INVERTER_H

#include <stdint.h>

/* ============================================================================
 * Single-phase 50 Hz inverter core for the M365 ESC (STM32F103).
 *
 * Reuses the EBiCS peripheral bring-up (clock/TIM1/ADC/GPIO) but replaces all
 * motor control: the fast ISR calls inverter_fast(), which writes a sine into
 * switchtime[] (applied to TIM1->CCR1..3). H-bridge across legs A (CH1) and
 * B (CH2); leg C (CH3) parked at 50%. TIM1 is center-aligned, ARR = _T = 2028,
 * clock 64 MHz  ->  ISR/PWM rate = 64e6 / (2*2028) = 15779 Hz.
 * ==========================================================================*/

/* ---- Output waveform ---- */
#define INV_CENTER        1014u     /* _T/2, 50% duty midpoint (ARR = 2028)      */
#define INV_PWM_HZ        15779u    /* control-ISR rate = 64MHz/(2*_T)           */
#define INV_OUT_HZ        50u       /* target output frequency                    */
#define INV_PHASE_STEP    ((uint32_t)(((uint64_t)INV_OUT_HZ << 32) / INV_PWM_HZ))
/* Amplitude slew = +1 step every N ISR ticks (ISR ~15779 Hz, full amp ~350).
 * Initial soft-start is slow so the transformer flux and any capacitive/SMPS load
 * charge gradually; after it first reaches target, recovery from a fold-back is fast. */
#define INV_RAMP_DIV      225u      /* ~5 s to full amplitude (soft-start)        */
#define INV_RECOVER_DIV   8u        /* ~0.18 s recovery back to target after a limit */

/* ---- Output amplitude ----
 * INV_REGULATE 0 : fixed open-loop amplitude INV_AMP_SET (robust, predictable).
 * INV_REGULATE 1 : feed-forward regulation from supply voltage — needs a
 *                  RELIABLE Vbat reading; the M365 regular-ADC scan is disturbed
 *                  by the 15.8 kHz injected conversions under load, so this is
 *                  OFF by default (bad Vbat -> amp runs to the ceiling -> overshoot).
 * Calibrate INV_AMP_SET on the bench: raise until the secondary reads the wanted
 * RMS. Reference: ~amp 358 gave ~230 V at ~30 V supply with the test transformer. */
#define INV_REGULATE      0
#define INV_AMP_SET       350       /* fixed amplitude (open-loop) — TUNE for your Vout */
#define INV_VOUT_TARGET   230       /* regulation target (only if INV_REGULATE 1)  */
#define INV_CAL_AMP       150       /* regulation calibration point ...            */
#define INV_CAL_VOUT      94        /* ... gave this secondary RMS ...             */
#define INV_CAL_VBAT_MV   32000     /* ... at this supply voltage (mV)             */
#define INV_AMP_MAX       420       /* hard amplitude ceiling (safety)             */

/* ---- Protection ---- */
#define INV_CAL_I         38        /* mA per phase-current ADC count (CAL_I)     */
/* Protection = "electronic breaker": the output holds full voltage (no droop);
 * overload is caught by tripping, not by limiting.
 *  - INVERSE-TIME overload latch on the AVERAGED current (thermal-magnetic breaker
 *    model): integrate how far the average is over ILIM; trip when the integral
 *    exceeds INV_OL_TRIP -> bigger overload trips faster, brief peaks decay away.
 *  - fast hard-latch on the INSTANTANEOUS peak for a genuine short. */
#define INV_ILIM_A        23        /* average current considered overload, amps   */
#define INV_ILIM_CNT      ((INV_ILIM_A * 1000) / INV_CAL_I)
#define INV_IAVG_SHIFT    6         /* current low-pass: TC ~ 2^6 ISR cycles (~4ms) */
#define INV_OL_TRIP       1000000   /* inverse-time integral threshold. Approx trip
                                     * times: ~1.2s @25A, ~0.35s @30A, ~0.15s @40A. */
#define INV_OC_TRIP_A     75        /* hard short-circuit latch, amps (peak); high so
                                     * brief capacitive/triac inrush peaks pass and
                                     * only a real short (ADC saturates ~78A) latches */
#define INV_OC_TRIP_CNT   ((INV_OC_TRIP_A * 1000) / INV_CAL_I)
#define INV_OC_DEBOUNCE   4         /* consecutive over-trip samples to latch      */
#define INV_OC_BLANK      1600      /* startup blanking: ~100ms of ISR cycles      */
/* Thermal fold-back. Disabled by default: the M365 temperature reading is
 * board-specific and the (counts*41)>>8 scaling is uncalibrated, so a stray/
 * unconnected sensor reads as a bogus high temperature and would wrongly hold
 * the output off. Calibrate the scaling for your board, then set INV_TEMP_ENABLE 1. */
#define INV_TEMP_ENABLE   1
#define INV_TEMP_LIMIT_C  60        /* thermal fold-back: stop above this degC    */
#define INV_TEMP_CLEAR_C  50        /* ... resume below this (hysteresis)         */

/* ---- Fault codes (inverter_fault) ---- */
#define INV_FAULT_NONE    0
#define INV_FAULT_OC      1         /* instantaneous short-circuit latch          */
#define INV_FAULT_OL      2         /* inverse-time overload latch                */

void    inverter_init(void);                            /* once, after peripherals up   */
void    inverter_fast(int16_t iph1, int16_t iph2);      /* every PWM ISR                */
void    inverter_slow(int32_t vbat_mV, int16_t temp_c); /* ~10 ms: regulation + thermal */
uint8_t inverter_fault(void);
int32_t inverter_amp(void);    /* current soft-started amplitude (telemetry)    */
int32_t inverter_ipeak(void);  /* peak |phase current| since last call, counts  */

#endif /* INVERTER_H */
