#include "audio.h"
#include "adc.h"
#include "tim.h"
#include "beat.h"

#define HALF_SAMPLES (AUDIO_DMA_SAMPLES/2U)
#define STALE_MS 100U
/* DMA持续写入，一半采样64点=8 ms。不能与任何效果缓冲共用。 */
__ALIGN_BEGIN static volatile uint16_t samples[AUDIO_DMA_SAMPLES] __ALIGN_END;
static volatile uint8_t running, fault, received, sensitivity, envelope;
static volatile uint16_t amplitude, bias, clipped;
static volatile uint32_t last_block_at, blocks;
static uint32_t retry_at, errors;
static int32_t dc_q8;
static uint8_t dc_ready;

static void StopSampling(void)
{
    running = 0U;
    (void)HAL_TIM_Base_Stop(&htim1);
    (void)HAL_ADC_Stop_DMA(&hadc);
    envelope = 0U;
    Beat_ClearPulse();
}

static void StartSampling(void)
{
    retry_at = HAL_GetTick();
    StopSampling();
    fault = 0U;
    received = 0U;
    dc_ready = 0U;
    Beat_Reset();
    amplitude = clipped = 0U;
    /* UG放在ADC准备之前，避免更新事件意外触发一次转换。 */
    __HAL_TIM_SET_COUNTER(&htim1, 0U);
    htim1.Instance->EGR = TIM_EGR_UG;
    __HAL_ADC_CLEAR_FLAG(&hadc, ADC_FLAG_OVR);
    if (HAL_ADCEx_Calibration_Start(&hadc) != HAL_OK ||
        HAL_ADC_Start_DMA(&hadc, (uint32_t *)samples, AUDIO_DMA_SAMPLES) != HAL_OK)
    {
        fault = 1U;
        errors++;
        StopSampling();
        return;
    }
    last_block_at = HAL_GetTick();
    running = 1U;
    if (HAL_TIM_Base_Start(&htim1) != HAL_OK)
    {
        fault = 1U;
        errors++;
        StopSampling();
    }
}

void Audio_Init(void)
{
    sensitivity = 2U;
    errors = 0U;
    blocks = 0U;
    bias = 2048U;
    StartSampling();
}

void Audio_Task(void)
{
    /* 先取样本时间再取当前时间，避免ADC中断更新时戳引起无符号差值下溢。 */
    uint32_t last = last_block_at;
    uint32_t now = HAL_GetTick();
    if (running && (fault || now-last > STALE_MS ||
                    __HAL_ADC_GET_FLAG(&hadc, ADC_FLAG_OVR)))
    {
        fault = 1U;
        StopSampling();
        errors++;
        retry_at = now;
    }
    /* 采样失效只熄灭声控效果，其余功能继续；每秒尝试恢复一次。 */
    if (!running && now-retry_at >= 1000U) StartSampling();
}

static uint8_t HalfIsStable(uint16_t offset)
{
    uint32_t left = __HAL_DMA_GET_COUNTER(hadc.DMA_Handle);
    if (offset == 0U) return left > 0U && left <= HALF_SAMPLES;
    return left > HALF_SAMPLES && left <= AUDIO_DMA_SAMPLES;
}

static void ProcessHalf(uint16_t offset)
{
    /* 该回调只做64点整数处理；不打印、不等待，红外优先级0可抢占它。 */
    static const uint16_t full_scales[5] = {320U,200U,128U,80U,48U};
    uint32_t i, sum = 0U, clip_count = 0U, target, value;
    int32_t delta;
    if (!running || fault) return;
    if (!HalfIsStable(offset)) { fault = 1U; return; }
    for (i = 0U; i < HALF_SAMPLES; i++)
    {
        value = samples[offset+i];
        if (value <= 8U || value >= 4087U) clip_count++;
        /* 仅跟踪直流偏置，不会将开机时的音乐学习成背景噪声。 */
        if (!dc_ready) { dc_q8 = (int32_t)value*256; dc_ready = 1U; }
        dc_q8 += ((int32_t)value*256-dc_q8)/256;
        delta = (int32_t)value-dc_q8/256;
        Beat_PushSample(delta);
        sum += (uint32_t)(delta < 0 ? -delta : delta);
    }
    /* 若处理期间DMA已绕回，丢弃本块并恢复采样，不能发布混合数据。 */
    if (!HalfIsStable(offset)) { fault = 1U; return; }
    amplitude = (uint16_t)(sum/HALF_SAMPLES);
    bias = (uint16_t)(dc_q8/256);
    clipped = (uint16_t)clip_count;
    target = amplitude > AUDIO_NOISE_GATE ? amplitude-AUDIO_NOISE_GATE : 0U;
    target = target*255U/full_scales[sensitivity];
    if (target > 255U) target = 255U;
    /* 8 ms一块：快速跟随上升，较慢回落；向上取整保证最终能回到全黑。 */
    if (target > envelope) envelope += (uint8_t)((target-envelope+1U)/2U);
    else envelope -= (uint8_t)((envelope-target+15U)/16U);
    Beat_EndBlock(sensitivity, HAL_GetTick());
    last_block_at = HAL_GetTick();
    blocks++;
    received = 1U;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc)
{
    if (adc->Instance == ADC1) ProcessHalf(0U);
}
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc)
{
    if (adc->Instance == ADC1) ProcessHalf(HALF_SAMPLES);
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc)
{
    if (adc->Instance == ADC1) { fault = 1U; envelope = 0U; Beat_ClearPulse(); }
}

void Audio_SetSensitivity(uint8_t index) { sensitivity = index > 4U ? 4U : index; }
Audio_Status Audio_GetStatus(void)
{
    uint32_t last = last_block_at;
    uint32_t now = HAL_GetTick();
    if (fault || !running || now-last > STALE_MS) return AUDIO_FAULT;
    return received ? AUDIO_OK : AUDIO_WAITING;
}
uint8_t Audio_GetLevel(void) { return Audio_GetStatus() == AUDIO_OK ? envelope : 0U; }
uint8_t Audio_GetPulseLevel(void) { return Audio_GetStatus() == AUDIO_OK ? Beat_PulseLevel() : 0U; }
uint16_t Audio_GetAmplitude(void) { return amplitude; }
uint16_t Audio_GetBias(void) { return bias; }
uint16_t Audio_GetClippedSamples(void) { return clipped; }
uint32_t Audio_GetErrorCount(void) { return errors; }
uint32_t Audio_GetBlockCount(void) { return blocks; }
