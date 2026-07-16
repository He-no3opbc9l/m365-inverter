#include "inverter.h"
#include "stm32f1xx_hal.h"
#include <arm_math.h>

static uint32_t         theta;         /* DDS phase accumulator (full circle = 2^32) */
static int32_t          amp;           /* current, soft-started amplitude            */
static volatile int32_t amp_target;    /* amplitude wanted by the slow loop          */
static uint16_t         rtick;         /* slew prescaler                             */
static volatile uint8_t fault;         /* INV_FAULT_* (latched)                      */
static volatile uint8_t thermal_hold;  /* non-latching thermal fold-back             */
static uint16_t         oc_cnt;        /* consecutive over-current samples           */
static uint32_t         oc_blank;      /* startup over-current blanking countdown    */
static volatile int32_t ipeak;         /* peak |phase current| (telemetry)           */
static uint8_t          started;       /* set once soft-start reaches target         */

void inverter_init(void)
{
    theta = 0;
    amp = 0;
    amp_target = 0;
    rtick = 0;
    fault = INV_FAULT_NONE;
    thermal_hold = 0;
    oc_cnt = 0;
    oc_blank = INV_OC_BLANK;
    ipeak = 0;
    started = 0;
    SET_BIT(TIM1->BDTR, TIM_BDTR_MOE);   /* enable the output stage */
}

uint8_t inverter_fault(void) { return fault; }
int32_t inverter_amp(void)   { return amp; }
int32_t inverter_ipeak(void) { int32_t p = ipeak; ipeak = 0; return p; }  /* read-and-clear */

/* Called every PWM period from the ADC injected-conversion ISR. iph1/iph2 are the
 * zero-centred phase-A/phase-B shunt currents (ADC counts). Writes TIM1->CCR1..3. */
void inverter_fast(int16_t iph1, int16_t iph2)
{
    /* --- fast overcurrent / short-circuit trip (debounced + startup-blanked) --- */
    int32_t ia = (iph1 < 0) ? -iph1 : iph1;
    int32_t ib = (iph2 < 0) ? -iph2 : iph2;
    int32_t imax = (ia > ib) ? ia : ib;
    if (imax > ipeak) ipeak = imax;                /* peak-hold for telemetry */
    if (oc_blank) oc_blank--;

    /* hard short-circuit latch (debounced, blanked at startup). No soft current-
     * limit fold-back: it collapsed the output into loads; the bench supply's own
     * current limit handles overload, this only catches a genuine short. */
    if (!oc_blank && imax > INV_OC_TRIP_CNT) {
        if (++oc_cnt >= INV_OC_DEBOUNCE)
            fault = INV_FAULT_OC;
    } else {
        oc_cnt = 0;
    }

    if (fault) {                                   /* latched: kill the output */
        CLEAR_BIT(TIM1->BDTR, TIM_BDTR_MOE);
        TIM1->CCR1 = INV_CENTER;
        TIM1->CCR2 = INV_CENTER;
        TIM1->CCR3 = INV_CENTER;
        return;
    }

    SET_BIT(TIM1->BDTR, TIM_BDTR_MOE);             /* keep output enabled */

    /* slew amplitude toward target: slow soft-start on power-up, fast recovery after */
    int32_t  tgt  = thermal_hold ? 0 : amp_target;
    uint16_t mask = started ? INV_RECOVER_MASK : INV_RAMP_MASK;
    if ((++rtick & mask) == 0) {
        if (amp < tgt)      amp++;
        else if (amp > tgt) amp--;
    }
    if (tgt > 0 && amp >= tgt) started = 1;

    /* DDS sine. arm_sin_q31 wants input in [0 .. 0x7FFFFFFF] = [0 .. 2*pi);
     * (theta >> 1) keeps it positive across the whole accumulator -> one clean
     * continuous sine per wrap (a signed cast would flatten half the wave). */
    theta += INV_PHASE_STEP;
    q31_t   s   = arm_sin_q31((q31_t)(theta >> 1));
    int32_t val = (int32_t)(((int64_t)s * amp) >> 31);   /* peak deviation = amp */

    TIM1->CCR1 = (uint16_t)(INV_CENTER + val);     /* leg A */
    TIM1->CCR2 = (uint16_t)(INV_CENTER - val);     /* leg B (antiphase)     */
    TIM1->CCR3 = (uint16_t)(INV_CENTER);           /* leg C parked (unused) */
}

/* Called ~every 10 ms. vbat_mV = supply voltage (mV), temp_c = heatsink degC. */
void inverter_slow(int32_t vbat_mV, int16_t temp_c)
{
    /* thermal fold-back with hysteresis (auto-recovers, not latched) */
#if INV_TEMP_ENABLE
    if (temp_c > INV_TEMP_LIMIT_C)      thermal_hold = 1;
    else if (temp_c < INV_TEMP_CLEAR_C) thermal_hold = 0;
#else
    (void)temp_c;
#endif

    if (fault) return;                             /* OC latched: stay off */

#if INV_REGULATE
    /* feed-forward regulation: amp needed to hold INV_VOUT_TARGET at this supply.
     * Requires a reliable Vbat reading (see INV_REGULATE note in inverter.h). */
    if (vbat_mV < 1000) { amp_target = 0; return; }
    int64_t num = (int64_t)INV_VOUT_TARGET * INV_CAL_AMP * INV_CAL_VBAT_MV;
    int32_t t   = (int32_t)(num / ((int64_t)INV_CAL_VOUT * vbat_mV));
    if (t > INV_AMP_MAX) t = INV_AMP_MAX;
    if (t < 0)           t = 0;
    amp_target = t;
#else
    (void)vbat_mV;
    amp_target = (INV_AMP_SET > INV_AMP_MAX) ? INV_AMP_MAX : INV_AMP_SET;
#endif
}
