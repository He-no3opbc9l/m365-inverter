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

/* ADC regular-scan (DMA) buffer indices (see bsp.c for the scan order) */
#define BSP_ADC_VBAT      0         /* ADC1_IN2  (PA2)  battery / supply voltage      */
#define BSP_ADC_TEMP      2         /* ADC1_IN0  (PA0)  temperature sensor            */
#define BSP_ADC_PHA       3         /* ADC1_IN3  (PA3)  phase-A shunt (offset cal)    */
#define BSP_ADC_PHB       4         /* ADC1_IN4  (PA4)  phase-B shunt (offset cal)    */
#define BSP_ADC_N         6         /* number of regular conversions                  */

/* Calibration constants (ADC counts -> engineering units) */
#define BSP_CAL_BAT_MV    13        /* battery voltage: counts * 13 = mV              */
#define BSP_TEMP_NUM      41        /* temperature degC = (counts * 41) >> 8          */

/* Status LED (PD1, needs PD01 remap) */
#define BSP_LED_PORT      GPIOD
#define BSP_LED_PIN       GPIO_PIN_1

/* Peripheral handles (defined in bsp.c) */
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
extern DMA_HandleTypeDef hdma_adc1;

/* Live regular-ADC results (DMA target): Vbat, temp, ... (BSP_ADC_* indices) */
extern volatile uint16_t bsp_adc[BSP_ADC_N];

void    bsp_clock_init(void);     /* 64 MHz from HSI/PLL                        */
void    bsp_init(void);           /* GPIO, TIM1/3, ADC1/2, DMA, shunt offsets   */
void    bsp_start_control(void);  /* enable the injected ISR (after inverter_init) */

/* read the two phase-current shunts sampled this PWM period (zero-centred counts) */
static inline int16_t bsp_iphase_a(void) { return (int16_t)hadc1.Instance->JDR1; }
static inline int16_t bsp_iphase_b(void) { return (int16_t)hadc2.Instance->JDR1; }

static inline int32_t bsp_vbat_mv(void)  { return (int32_t)bsp_adc[BSP_ADC_VBAT] * BSP_CAL_BAT_MV; }
static inline int16_t bsp_temp_c(void)   { return (int16_t)(((int32_t)bsp_adc[BSP_ADC_TEMP] * BSP_TEMP_NUM) >> 8); }

#endif /* BSP_H */
