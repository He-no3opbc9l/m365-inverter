/* stm32f1xx_hal_msp.c - minimal MSP for m365-inverter
 * ST HAL MSP callbacks: peripheral clocks, pins and NVIC.
 * (c) STMicroelectronics BSD-3-Clause for the HAL; this trimmed file GPLv3.
 */
#include "main.h"
#include "bsp.h"

extern DMA_HandleTypeDef hdma_adc1;

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    __HAL_AFIO_REMAP_SWJ_NOJTAG();     /* keep SWD, free the JTAG pins */
}

/* ---- ADC: analog pins, ADC1 DMA, shared ADC1_2 interrupt ---- */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
    GPIO_InitTypeDef g = {0};

    if (hadc->Instance == ADC1) {
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        /* PA0 temp, PA2 Vbat, PA3 phaseA, PA4 phaseB, PA5/6/7 spare -> analog */
        g.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4
              | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
        g.Mode = GPIO_MODE_ANALOG;
        HAL_GPIO_Init(GPIOA, &g);

        hdma_adc1.Instance = DMA1_Channel1;
        hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
        hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        hdma_adc1.Init.Mode = DMA_CIRCULAR;
        hdma_adc1.Init.Priority = DMA_PRIORITY_MEDIUM;
        if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) Error_Handler();
        __HAL_LINKDMA(hadc, DMA_Handle, hdma_adc1);

        HAL_NVIC_SetPriority(ADC1_2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
    } else if (hadc->Instance == ADC2) {
        __HAL_RCC_ADC2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        HAL_NVIC_SetPriority(ADC1_2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
    }
}

/* ---- Timers: clocks only (no timer interrupts are used) ---- */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) __HAL_RCC_TIM1_CLK_ENABLE();
    else if (htim->Instance == TIM3) __HAL_RCC_TIM3_CLK_ENABLE();
}

/* ---- TIM1 PWM output pins ---- */
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim)
{
    GPIO_InitTypeDef g = {0};
    if (htim->Instance == TIM1) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        /* PB13/14/15 -> CH1N/2N/3N ; PA8/9/10 -> CH1/2/3 */
        g.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
        g.Mode = GPIO_MODE_AF_PP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &g);
        g.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
        HAL_GPIO_Init(GPIOA, &g);
    }
}

/* ---- USART1 TX (PB6, remapped) for telemetry, blocking - no DMA ---- */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef g = {0};
    if (huart->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        g.Pin = GPIO_PIN_6;
        g.Mode = GPIO_MODE_AF_OD;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOB, &g);
        __HAL_AFIO_REMAP_USART1_ENABLE();   /* USART1 TX -> PB6 */
    }
}
