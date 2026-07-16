#include "bsp.h"

/* Injected-channel select values for ADC->JSQR (1 conversion, JSQ4 = channel) */
#define JSQR_PHASE_A  (3u << 15)   /* ADC1 injected -> IN3 (PA3) phase-A shunt */
#define JSQR_PHASE_B  (4u << 15)   /* ADC2 injected -> IN4 (PA4) phase-B shunt */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);   /* defined in stm32f1xx_hal_msp.c */

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;
DMA_HandleTypeDef hdma_adc1;

volatile uint16_t bsp_adc[BSP_ADC_N];

static void err(void) { while (1) { } }

/* ---------------------------------------------------------------- clock ---- */
void bsp_clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    RCC_PeriphCLKInitTypeDef pclk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = 16;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;   /* 8MHz / 2 = 4 MHz */
    osc.PLL.PLLMUL = RCC_PLL_MUL16;               /* 4 MHz * 16 = 64 MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) err();

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                  | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;          /* HCLK  = 64 MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV2;           /* PCLK1 = 32 MHz */
    clk.APB2CLKDivider = RCC_HCLK_DIV1;           /* PCLK2 = 64 MHz (TIM1, ADC) */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) err();

    pclk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    pclk.AdcClockSelection = RCC_ADCPCLK2_DIV6;   /* ADC clock = 64/6 ~= 10.7 MHz */
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) err();

    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);
    HAL_NVIC_SetPriority(SysTick_IRQn, 2, 0);
}

/* ----------------------------------------------------------------- gpio ---- */
static void gpio_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    __HAL_AFIO_REMAP_PD01_ENABLE();               /* free PD0/PD1 for GPIO (LED on PD1) */

    HAL_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, GPIO_PIN_RESET);
    g.Pin = BSP_LED_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BSP_LED_PORT, &g);
    /* TIM1 CH/CHN pins and the ADC analog pins are configured by the HAL MSP
     * callbacks (HAL_TIM_MspPostInit / HAL_ADC_MspInit) when the peripherals
     * below are initialised. */
}

static void dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

/* ----------------------------------------------------------------- TIM1 ---- */
static void tim1_init(void)
{
    TIM_ClockConfigTypeDef clk = {0};
    TIM_MasterConfigTypeDef mst = {0};
    TIM_OC_InitTypeDef oc = {0};
    TIM_BreakDeadTimeConfigTypeDef bdt = {0};

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
    htim1.Init.Period = BSP_TIM_PERIOD;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim1) != HAL_OK) err();

    clk.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim1, &clk) != HAL_OK) err();
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) err();
    if (HAL_TIM_OC_Init(&htim1) != HAL_OK) err();

    mst.MasterOutputTrigger = TIM_TRGO_OC4REF;    /* CC4 -> triggers injected ADC */
    mst.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &mst) != HAL_OK) err();

    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 1;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_SET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1) != HAL_OK) err();
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_2) != HAL_OK) err();
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_3) != HAL_OK) err();

    oc.OCMode = TIM_OCMODE_PWM2;                  /* CH4: ADC trigger reference */
    oc.Pulse = BSP_TIM_PERIOD - 1;
    if (HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_4) != HAL_OK) err();

    bdt.OffStateRunMode = TIM_OSSR_DISABLE;
    bdt.OffStateIDLEMode = TIM_OSSI_DISABLE;
    bdt.LockLevel = TIM_LOCKLEVEL_OFF;
    bdt.DeadTime = BSP_DEADTIME;
    bdt.BreakState = TIM_BREAK_DISABLE;
    bdt.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    bdt.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &bdt) != HAL_OK) err();

    HAL_TIM_MspPostInit(&htim1);                  /* configure CH/CHN GPIO pins */
}

/* ----- TIM3: periodic trigger (TRGO) that paces the ADC1 regular scan ------ */
static void tim3_init(void)
{
    TIM_ClockConfigTypeDef clk = {0};
    TIM_MasterConfigTypeDef mst = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 7813;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) err();

    clk.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &clk) != HAL_OK) err();

    mst.MasterOutputTrigger = TIM_TRGO_OC1;
    mst.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &mst) != HAL_OK) err();
}

/* ----------------------------------------------------------------- ADC ----- */
static void adc1_init(void)
{
    ADC_MultiModeTypeDef mm = {0};
    ADC_InjectionConfTypeDef inj = {0};
    ADC_ChannelConfTypeDef ch = {0};

    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;  /* regular scan paced by TIM3 */
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = BSP_ADC_N;
    hadc1.Init.NbrOfDiscConversion = 0;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) err();

    mm.Mode = ADC_DUALMODE_REGSIMULT_INJECSIMULT;  /* ADC1+ADC2 injected fire together */
    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &mm) != HAL_OK) err();

    inj.InjectedChannel = ADC_CHANNEL_3;           /* phase-A shunt */
    inj.InjectedRank = ADC_INJECTED_RANK_1;
    inj.InjectedNbrOfConversion = 1;
    inj.InjectedSamplingTime = ADC_SAMPLETIME_1CYCLE_5;
    inj.ExternalTrigInjecConv = ADC_EXTERNALTRIGINJECCONV_T1_CC4;
    inj.AutoInjectedConv = DISABLE;
    inj.InjectedDiscontinuousConvMode = DISABLE;
    inj.InjectedOffset = 0;
    HAL_ADC_Stop(&hadc1);
    if (HAL_ADCEx_InjectedConfigChannel(&hadc1, &inj) != HAL_OK) err();

    /* regular scan order -> bsp_adc[]: Vbat, IN7, temp, phaseA, phaseB, phaseC */
    const uint32_t chan[BSP_ADC_N] = {
        ADC_CHANNEL_2, ADC_CHANNEL_7, ADC_CHANNEL_0,
        ADC_CHANNEL_3, ADC_CHANNEL_4, ADC_CHANNEL_5
    };
    const uint32_t rank[BSP_ADC_N] = {
        ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ADC_REGULAR_RANK_3,
        ADC_REGULAR_RANK_4, ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6
    };
    for (int i = 0; i < BSP_ADC_N; i++) {
        ch.Channel = chan[i];
        ch.Rank = rank[i];
        ch.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
        if (HAL_ADC_ConfigChannel(&hadc1, &ch) != HAL_OK) err();
    }
}

static void adc2_init(void)
{
    ADC_InjectionConfTypeDef inj = {0};

    hadc2.Instance = ADC2;
    hadc2.Init.ScanConvMode = ADC_SCAN_ENABLE;
    hadc2.Init.ContinuousConvMode = DISABLE;
    hadc2.Init.DiscontinuousConvMode = DISABLE;
    hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc2.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&hadc2) != HAL_OK) err();

    inj.InjectedChannel = ADC_CHANNEL_4;           /* phase-B shunt */
    inj.InjectedRank = ADC_INJECTED_RANK_1;
    inj.InjectedNbrOfConversion = 1;
    inj.InjectedSamplingTime = ADC_SAMPLETIME_1CYCLE_5;
    inj.ExternalTrigInjecConv = ADC_INJECTED_SOFTWARE_START; /* slaved via dual mode */
    inj.AutoInjectedConv = DISABLE;
    inj.InjectedDiscontinuousConvMode = DISABLE;
    inj.InjectedOffset = 0;
    if (HAL_ADCEx_InjectedConfigChannel(&hadc2, &inj) != HAL_OK) err();
}

/* ------------------------------------------------------------- bring-up ---- */
void bsp_init(void)
{
    gpio_init();
    dma_init();

    adc1_init();
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK) err();
    adc2_init();
    if (HAL_ADCEx_Calibration_Start(&hadc2) != HAL_OK) err();

    /* arm injected conversions off the TIM1_CC4 hardware trigger, but leave the
     * JEOC interrupt DISABLED for now so the control loop does not run (and does
     * not force the output on) while we measure the zero-current shunt offsets. */
    SET_BIT(ADC1->CR2, ADC_CR2_JEXTTRIG);
    SET_BIT(ADC2->CR2, ADC_CR2_JEXTTRIG);

    HAL_ADCEx_MultiModeStart_DMA(&hadc1, (uint32_t *)bsp_adc, BSP_ADC_N);
    HAL_ADC_Start_IT(&hadc2);

    tim1_init();
    tim3_init();

    if (HAL_TIM_Base_Start(&htim1) != HAL_OK) err();
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
    TIM1->CCR4 = BSP_ADC_TRIGGER;                  /* fixed injected-ADC sample point */

    if (HAL_TIM_Base_Start(&htim3) != HAL_OK) err();   /* start pacing the regular scan */

    /* park at 50% and keep the output disabled while we measure the shunt offsets */
    CLEAR_BIT(TIM1->BDTR, TIM_BDTR_MOE);
    TIM1->CCR1 = BSP_TIM_PERIOD / 2;
    TIM1->CCR2 = BSP_TIM_PERIOD / 2;
    TIM1->CCR3 = BSP_TIM_PERIOD / 2;

    HAL_Delay(500);

    uint32_t off_a = 0, off_b = 0;
    for (int i = 0; i < 16; i++) {
        HAL_Delay(5);
        off_a += bsp_adc[BSP_ADC_PHA];
        off_b += bsp_adc[BSP_ADC_PHB];
    }
    off_a >>= 4;
    off_b >>= 4;

    /* load the zero-current offsets so the injected data registers read signed */
    ADC1->JSQR = JSQR_PHASE_A;
    ADC1->JOFR1 = off_a;
    ADC2->JSQR = JSQR_PHASE_B;
    ADC2->JOFR1 = off_b;
}

/* Enable the injected end-of-conversion interrupt -> starts the control loop.
 * Call this AFTER inverter_init(), once it is safe for the output to drive. */
void bsp_start_control(void)
{
    __HAL_ADC_ENABLE_IT(&hadc1, ADC_IT_JEOC);
    __HAL_ADC_ENABLE_IT(&hadc2, ADC_IT_JEOC);
}
