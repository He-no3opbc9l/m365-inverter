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
#define INV_RAMP_MASK     0x0Fu     /* amplitude slews +/-1 every 16 ISR ticks    */

/* ---- Closed-loop output regulation (feed-forward from supply voltage) ----
 * Holds the secondary RMS at INV_VOUT_TARGET regardless of supply voltage.
 * Calibration point measured on the bench: amp INV_CAL_AMP produced INV_CAL_VOUT
 * volts RMS at supply INV_CAL_VBAT_MV. Output is linear in amp. */
#define INV_VOUT_TARGET   230       /* desired secondary RMS, volts               */
#define INV_CAL_AMP       150       /* calibration: this amplitude ...            */
#define INV_CAL_VOUT      94        /* ... gave this secondary RMS ...            */
#define INV_CAL_VBAT_MV   32000     /* ... at this supply voltage (mV)            */
#define INV_AMP_MAX       420       /* hard amplitude ceiling (safety, ~260V)     */

/* ---- Protection ---- */
#define INV_CAL_I         38        /* mA per phase-current ADC count (CAL_I)     */
#define INV_OC_TRIP_A     10        /* overcurrent / short-circuit trip, amps     */
#define INV_OC_TRIP_CNT   ((INV_OC_TRIP_A * 1000) / INV_CAL_I)
#define INV_TEMP_LIMIT_C  80        /* thermal fold-back: stop above this degC    */
#define INV_TEMP_CLEAR_C  70        /* ... resume below this (hysteresis)         */

/* ---- Fault codes (inverter_fault) ---- */
#define INV_FAULT_NONE    0
#define INV_FAULT_OC      1         /* latched over-current; needs power cycle    */

void    inverter_init(void);                            /* once, after peripherals up   */
void    inverter_fast(int16_t iph1, int16_t iph2);      /* every PWM ISR                */
void    inverter_slow(int32_t vbat_mV, int16_t temp_c); /* ~10 ms: regulation + thermal */
uint8_t inverter_fault(void);
int32_t inverter_amp(void);    /* current soft-started amplitude (telemetry) */

#endif /* INVERTER_H */
