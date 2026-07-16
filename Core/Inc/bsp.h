#ifndef BSP_H
#define BSP_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* ============================================================================
 * Board support: peripheral bring-up for the Xiaomi M365 ESC (STM32F103C8).
 * Derived from the proven EBiCS/SmartESC init, stripped to what an inverter
 * needs: TIM1 (complementary PWM), ADC1/ADC2 (phase-current shunts, injected,
 * triggered by TIM1_CC4), ADC1 regular scan via DMA (Vbat, temperature), and
 * a periodic trigger timer (TIM3). No hall sensors, no FOC.
 * ==========================================================================*/

/* TIM1 PWM (center-aligned): period and ADC-trigger compare */
#define BSP_TIM_PERIOD    2028      /* ARR = _T; f_pwm = 64MHz/(2*ARR) = 15779 Hz */
#define BSP_ADC_TRIGGER   2020      /* CC4: injected-ADC sample near the counter top */
#define BSP_DEADTIME      32        /* TIM1 dead-time (counts)                        */

/* Calibration constants (ADC counts -> engineering units) */
#define BSP_CAL_BAT_MV    13        /* battery voltage: counts * 13 = mV              */

/* NTC temperature sensor: raw ADC counts DECREASE as temperature rises.
 * Bench-calibrated (rough, 2 close points): ~30C -> ~1790 counts,
 * ~40C -> ~1590 counts (=> ~20 counts/degC). Linear approximation; refine the
 * points with a real thermometer for accuracy at high temperature. */
#define BSP_TEMP_RAW0     1790      /* counts at BSP_TEMP_T0                          */
#define BSP_TEMP_T0       30        /* degC reference                                */
#define BSP_TEMP_SLOPE    20        /* counts per degC (raw falls as temp rises)     */

/* Status LED (PD1, needs PD01 remap) */
#define BSP_LED_PORT      GPIOD
#define BSP_LED_PIN       GPIO_PIN_1

/* Peripheral handles (defined in bsp.c) */
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;

void    bsp_clock_init(void);     /* 64 MHz from HSI/PLL                        */
void    bsp_init(void);           /* GPIO, TIM1/3, ADC1/2, shunt offsets        */
void    bsp_start_control(void);  /* enable the injected ISR (after inverter_init) */

/* All four analog reads come from the injected group (hardware-triggered by
 * TIM1_CC4, immune to load): ADC1 JDR1=phase-A(signed), JDR2=Vbat, JDR3=temp;
 * ADC2 JDR1=phase-B(signed). */
static inline int16_t bsp_iphase_a(void) { return (int16_t)hadc1.Instance->JDR1; }
static inline int16_t bsp_iphase_b(void) { return (int16_t)hadc2.Instance->JDR1; }

static inline int32_t bsp_vbat_mv(void)  { return (int32_t)(hadc1.Instance->JDR2 & 0xFFFF) * BSP_CAL_BAT_MV; }
static inline int16_t bsp_temp_c(void)
{
    int32_t raw = hadc1.Instance->JDR3 & 0xFFFF;
    return (int16_t)(BSP_TEMP_T0 + (BSP_TEMP_RAW0 - raw) / BSP_TEMP_SLOPE);
}

#endif /* BSP_H */
