#include "led.h"
#include "tim.h"

/* 48 MHz / 60 = 800 kHz。DMA 内存为 Byte，CCR 外设为 Half Word。 */
#define RESET_SLOTS 256U
#define PREFIX_SLOTS 2U
#define BUFFER_SIZE (PREFIX_SLOTS + LED_COUNT * 24U + RESET_SLOTS)
__ALIGN_BEGIN static uint8_t pwm[BUFFER_SIZE] __ALIGN_END;
static volatile uint8_t busy;
static volatile uint32_t errors;
static volatile uint32_t stopped_at;
static uint32_t started_at;

static void Stop(void)
{
    (void)HAL_TIM_PWM_Stop_DMA(&htim3, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    /* 刷新预装载值，异常中止后也让输出回到低电平。 */
    htim3.Instance->EGR = TIM_EGR_UG;
    stopped_at = HAL_GetTick();
    busy = 0U;
}

void LED_Init(void)
{
    busy = 0U;
    errors = 0U;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    htim3.Instance->EGR = TIM_EGR_UG;
    stopped_at = HAL_GetTick();
}

uint8_t LED_Ready(void)
{
    /* 一帧约 2.54 ms；超时后停止传输，避免永久卡在 busy。 */
    if (busy && (HAL_GetTick() - started_at > 20U))
    {
        Stop();
        errors++;
    }
    return !busy && (HAL_GetTick() - stopped_at >= 1U);
}

uint8_t LED_Show(const LED_Color *pixels, uint8_t brightness)
{
    uint32_t i, bit, grb, offset;
    if (!LED_Ready()) return 0U;
    if (brightness > LED_MAX_BRIGHTNESS) brightness = LED_MAX_BRIGHTNESS;
    for (i = 0U; i < LED_COUNT; i++)
    {
        /* 最后统一缩放亮度，任何效果都不能绕过 30% 上限。 */
        uint32_t r = (uint32_t)pixels[i].r * brightness / 100U;
        uint32_t g = (uint32_t)pixels[i].g * brightness / 100U;
        uint32_t b = (uint32_t)pixels[i].b * brightness / 100U;
        grb = (g << 16U) | (r << 8U) | b;
        offset = PREFIX_SLOTS + i * 24U;
        for (bit = 0U; bit < 24U; bit++)
            pwm[offset + bit] = (grb & (1UL << (23U - bit))) ? 38U : 19U;
    }
    /* 前缀和末尾始终为零：末尾提供 320 us 的复位低电平。 */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0U);
    htim3.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    started_at = HAL_GetTick();
    busy = 1U;
    if (HAL_TIM_PWM_Start_DMA(&htim3, TIM_CHANNEL_3,
                              (const uint32_t *)pwm, BUFFER_SIZE) != HAL_OK)
    {
        Stop();
        errors++;
        return 0U;
    }
    return 1U;
}

uint32_t LED_ErrorCount(void) { return errors; }

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3)
        Stop();
}

void HAL_TIM_ErrorCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        Stop();
        errors++;
    }
}
