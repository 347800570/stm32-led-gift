#include "beat.h"
#include "main.h"

#if BEAT_HP_COEFF_Q8 <= 0 || BEAT_HP_COEFF_Q8 >= 256 || BEAT_LP_COEFF_Q8 <= 0 || BEAT_LP_COEFF_Q8 >= 256
#error Invalid beat filter coefficient
#endif
#if BEAT_PULSE_MS == 0 || BEAT_REFRACTORY_MS == 0 || BEAT_WARMUP_BLOCKS == 0
#error Invalid beat timing
#endif
static int32_t low_dc, low1, low2;
static uint32_t sum_energy;
static uint16_t sample_count;
static uint8_t warmup, armed;
static volatile uint8_t pulse_active, has_beat;
static volatile uint32_t energy, background, last_beat, beat_count;
static volatile Beat_Diagnostics diagnostics;

void Beat_Reset(void)
{
    low_dc = low1 = low2 = 0;
    sum_energy = sample_count = warmup = 0U;
    energy = background = last_beat = beat_count = 0U;
    pulse_active = has_beat = 0U;
    armed = 1U;
    diagnostics = (Beat_Diagnostics){0};
}

void Beat_PushSample(int32_t sample)
{
    int32_t input = sample*256;
    int32_t band;
    low_dc += (input-low_dc)*BEAT_HP_COEFF_Q8/256;
    low1 += (input-low_dc-low1)*BEAT_LP_COEFF_Q8/256;
    low2 += (low1-low2)*BEAT_LP_COEFF_Q8/256;
    band = low2/256;
    /* 能量单位为ADC码值平方/16，提前缩放防止整块累加溢出。 */
    sum_energy += (uint32_t)(band*band)/16U;
    sample_count++;
}

void Beat_EndBlock(uint8_t sensitivity, uint32_t now)
{
    /* 档位越高：绝对门槛更低、所需相对增幅更小。 */
    static const uint16_t floors[5] = {144U,100U,64U,36U,16U};
    static const uint16_t ratios[5] = {300U,250U,200U,150U,100U};
    uint32_t instant, previous = energy, floor, threshold;
    uint8_t result;
    if (!sample_count) return;
    instant = sum_energy/sample_count;
    sum_energy = sample_count = 0U;
    energy = (previous+instant)/2U;
    if (sensitivity > 4U) sensitivity = 4U;
    floor = floors[sensitivity];
    /* 启动前128 ms只建立滤波/背景状态，避免启动瞬态误触发。 */
    if (warmup < BEAT_WARMUP_BLOCKS)
    {
        background = energy;
        warmup++;
        return;
    }
    threshold = background*ratios[sensitivity]/100U + floor;
    /* 门槛和背景必须与峰值来自同一块，不能用打印时的瞬时值配对。 */
    if (!diagnostics.valid || energy > diagnostics.peak)
    {
        diagnostics.peak = energy;
        diagnostics.threshold = threshold;
        diagnostics.background = background;
        diagnostics.sensitivity = sensitivity+1U;
        diagnostics.valid = 1U;
    }
    if (energy <= floor || energy <= background*130U/100U) armed = 1U;
    if (energy <= threshold) result = BEAT_BELOW;
    else if (energy <= previous+floor/4U+1U) result = BEAT_NO_RISE;
    else if (!armed) result = BEAT_NOT_ARMED;
    else if (has_beat && now-last_beat < BEAT_REFRACTORY_MS) result = BEAT_LOCKED;
    else result = BEAT_HIT;
    if (diagnostics.results[result] < 65535U) diagnostics.results[result]++;
    if (result == BEAT_HIT)
    {
        last_beat = now;
        pulse_active = has_beat = 1U;
        beat_count++;
        armed = 0U;
    }
    /* 约256 ms背景跟踪。先判定再更新，避免鼓点把自身门槛抬高。 */
    if (energy > background) background += (energy-background+31U)/32U;
    else background -= (background-energy+31U)/32U;
}

void Beat_ClearPulse(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    pulse_active = 0U;
    __set_PRIMASK(saved);
}

void Beat_TakeDiagnostics(Beat_Diagnostics *out)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    *out = diagnostics;
    diagnostics = (Beat_Diagnostics){0};
    __set_PRIMASK(saved);
}

uint8_t Beat_PulseLevel(void)
{
    uint32_t age, saved = __get_PRIMASK();
    uint8_t active;
    __disable_irq();
    age = HAL_GetTick()-last_beat;
    active = pulse_active;
    if (age >= BEAT_PULSE_MS) pulse_active = active = 0U;
    __set_PRIMASK(saved);
    if (!active) return 0U;
    return (uint8_t)((BEAT_PULSE_MS-age)*255U/BEAT_PULSE_MS);
}
uint32_t Beat_Energy(void) { return energy; }
uint32_t Beat_Background(void) { return background; }
uint32_t Beat_Count(void) { return beat_count; }
