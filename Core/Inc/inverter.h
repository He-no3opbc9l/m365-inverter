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
#define INV_RAMP_MASK     0x7Fu     /* initial soft-start: +1 every 128 ISR ticks (~2.8s)
                                     * so transformer flux builds gradually at power-on. */
#define INV_RECOVER_MASK  0x0Fu     /* AFTER soft-start: +1 every 16 ticks (~0.35s) so the
                                     * output recovers quickly once a load-induced fold-back
                                     * clears (otherwise a transient collapses it for seconds). */

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
/* Hard short-circuit latch: debounced + blanked at start-up. There is no soft
 * current-limit fold-back (it collapsed the output into normal loads); the bench
 * supply's own current limit handles overload, this only trips on a real short. */
#define INV_OC_TRIP_A     25        /* short-circuit latch, amps (phase)          */
#define INV_OC_TRIP_CNT   ((INV_OC_TRIP_A * 1000) / INV_CAL_I)
#define INV_OC_DEBOUNCE   4         /* consecutive over-trip samples to latch     */
#define INV_OC_BLANK      1600      /* startup blanking: ~100ms of ISR cycles     */
/* Thermal fold-back. Disabled by default: the M365 temperature reading is
 * board-specific and the (counts*41)>>8 scaling is uncalibrated, so a stray/
 * unconnected sensor reads as a bogus high temperature and would wrongly hold
 * the output off. Calibrate the scaling for your board, then set INV_TEMP_ENABLE 1. */
#define INV_TEMP_ENABLE   1
#define INV_TEMP_LIMIT_C  60        /* thermal fold-back: stop above this degC    */
#define INV_TEMP_CLEAR_C  50        /* ... resume below this (hysteresis)         */

/* ---- Fault codes (inverter_fault) ---- */
#define INV_FAULT_NONE    0
#define INV_FAULT_OC      1         /* latched over-current; needs power cycle    */

void    inverter_init(void);                            /* once, after peripherals up   */
void    inverter_fast(int16_t iph1, int16_t iph2);      /* every PWM ISR                */
void    inverter_slow(int32_t vbat_mV, int16_t temp_c); /* ~10 ms: regulation + thermal */
uint8_t inverter_fault(void);
int32_t inverter_amp(void);    /* current soft-started amplitude (telemetry)    */
int32_t inverter_ipeak(void);  /* peak |phase current| since last call, counts  */

#endif /* INVERTER_H */
