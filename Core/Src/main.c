/* ============================================================================
 * m365-inverter : 50 Hz single-phase inverter firmware for the Xiaomi M365 ESC
 *
 * Repurposes the ESC's 3-phase bridge as an H-bridge (legs A/B) driving a
 * transformer primary with center-aligned SPWM. Soft-start, feed-forward
 * output-voltage regulation, latched over-current and thermal fold-back.
 * See README.md.
 *
 * Based on the peripheral bring-up of EBiCS / SmartESC_STM32_v3 (GPLv3).
 * ==========================================================================*/

#include "stm32f1xx_hal.h"
#include "bsp.h"
#include "inverter.h"
#include <stdio.h>

UART_HandleTypeDef huart1;

void SystemClock_Config(void) { bsp_clock_init(); }

static void uart_init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);
}

/* ---- systick (1 kHz): HAL tick + the 10 ms inverter slow loop ---- */
void UserSysTickHandler(void)
{
    static uint32_t c;
    if ((++c % 10) == 0)
        inverter_slow(bsp_vbat_mv(), bsp_temp_c());
}

/* ---- fast control loop: fires on every TIM1_CC4 injected ADC conversion ---- */
void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)                    /* master of the dual pair */
        inverter_fast(bsp_iphase_a(), bsp_iphase_b());
}

int main(void)
{
    HAL_Init();
    bsp_clock_init();
    uart_init();
    bsp_init();          /* GPIO, TIM1/3, ADC1/2, DMA, shunt-offset calibration */
    inverter_init();     /* reset control state + enable the output stage       */
    bsp_start_control(); /* now start the fast control ISR                      */

    char line[96];
    uint32_t last = 0;
    for (;;) {
        uint32_t now = HAL_GetTick();
        if (now - last >= 500) {                   /* telemetry twice a second */
            last = now;
            int32_t vbat = bsp_vbat_mv();
            int32_t amp  = inverter_amp();
            int32_t vout = (int32_t)(((int64_t)INV_CAL_VOUT * amp * vbat)
                                     / ((int64_t)INV_CAL_AMP * INV_CAL_VBAT_MV));
            int n = snprintf(line, sizeof line,
                             "Vbat=%ld.%02ldV  Vout~%ldV  amp=%ld  T=%dC  fault=%u\r\n",
                             vbat / 1000, (vbat % 1000) / 10, (long)vout, (long)amp,
                             bsp_temp_c(), inverter_fault());
            HAL_UART_Transmit(&huart1, (uint8_t *)line, n, 50);

            if (inverter_fault())
                HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, GPIO_PIN_SET);
            else
                HAL_GPIO_TogglePin(BSP_LED_PORT, BSP_LED_PIN);
        }
    }
}

void Error_Handler(void) { __disable_irq(); while (1) { } }
