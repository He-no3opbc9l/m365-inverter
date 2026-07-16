/* stm32f1xx_it.c - interrupt handlers for m365-inverter */
#include "main.h"
#include "stm32f1xx_it.h"
#include "bsp.h"
#include "inverter.h"

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

/* ---- ADC injected end-of-conversion -> fast control loop ----
 * Bare-metal (no HAL_ADC_IRQHandler): the HAL handler disables the injected IT
 * when its state doesn't match a *_Start_IT call. We drive the injected group in
 * polling mode and take the JEOC interrupt directly. ADC1 is the dual-mode
 * master; when its injected sequence (phaseA, Vbat, temp) completes, ADC2's
 * phaseB has completed simultaneously too. */
void ADC1_2_IRQHandler(void)
{
    if (READ_BIT(ADC1->SR, ADC_SR_JEOC)) {
        CLEAR_BIT(ADC1->SR, ADC_SR_JEOC);
        inverter_fast(bsp_iphase_a(), bsp_iphase_b());
    }
}
