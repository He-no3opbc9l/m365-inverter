/* stm32f1xx_it.c - interrupt handlers for m365-inverter */
#include "main.h"
#include "stm32f1xx_it.h"
#include "bsp.h"

/* ---- Cortex-M system handlers ---- */
void NMI_Handler(void)        { while (1) { } }
void HardFault_Handler(void)  { while (1) { } }
void MemManage_Handler(void)  { while (1) { } }
void BusFault_Handler(void)   { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
void SVC_Handler(void)        { }
void DebugMon_Handler(void)   { }
void PendSV_Handler(void)     { }

void SysTick_Handler(void)
{
    HAL_IncTick();
    UserSysTickHandler();
}

/* ---- ADC1/ADC2 injected end-of-conversion -> fast control loop ---- */
void ADC1_2_IRQHandler(void)
{
    HAL_ADC_IRQHandler(&hadc1);
    HAL_ADC_IRQHandler(&hadc2);
}

/* ---- ADC1 regular-scan DMA (Vbat / temperature) ---- */
void DMA1_Channel1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_adc1);
}
