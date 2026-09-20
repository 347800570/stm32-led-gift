#include "led.h"
#include "tim.h"

#define WS2812_BITS_PER_LED       24U
#define WS2812_RESET_SLOTS        64U
#define WS2812_BUFFER_SIZE        ((LED_COUNT * WS2812_BITS_PER_LED) + WS2812_RESET_SLOTS)

/* TIM3 runs at 48 MHz with ARR = 59, so one PWM period is 1.25 us. */
#define WS2812_DUTY_0             19U
#define WS2812_DUTY_1             38U
#define WS2812_TIM_CHANNEL        TIM_CHANNEL_3

typedef struct
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} LED_ColorTypeDef;

typedef enum
{
  LED_TEST_FILLING = 0,
  LED_TEST_FULL_HOLD,
  LED_TEST_OFF_HOLD
} LED_TestStateTypeDef;

/* Duty values are below 256, so byte-wide DMA memory saves half the RAM. */
static uint8_t led_pwm_buffer[WS2812_BUFFER_SIZE];
static volatile uint8_t led_dma_busy;
static uint8_t led_brightness;
static uint8_t led_test_color_index;
static uint32_t led_lit_count;
static uint32_t led_last_step_tick;
static LED_TestStateTypeDef led_test_state;

static const LED_ColorTypeDef led_test_colors[] =
{
  {255U, 0U, 0U},
  {0U, 255U, 0U},
  {0U, 0U, 255U}
};

static uint8_t LED_ApplyBrightness(uint8_t value);
static void LED_EncodePixel(uint32_t pixel_index, LED_ColorTypeDef color);
static void LED_AppendResetSlots(void);
static void LED_BuildTestFrame(uint32_t lit_count);
static HAL_StatusTypeDef LED_StartTransfer(void);

void LED_Init(void)
{
  led_dma_busy = 0U;
  led_brightness = LED_TEST_BRIGHTNESS;
  led_test_color_index = 0U;
  led_lit_count = 0U;
  led_test_state = LED_TEST_FILLING;

  /* Make the first task call light the first pixel immediately. */
  led_last_step_tick = HAL_GetTick() - LED_TEST_STEP_INTERVAL_MS;
  __HAL_TIM_SET_COMPARE(&htim3, WS2812_TIM_CHANNEL, 0U);
}

void LED_ContinuityTestTask(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t interval;

  if (led_dma_busy != 0U)
  {
    return;
  }

  if (led_test_state == LED_TEST_FILLING)
  {
    interval = LED_TEST_STEP_INTERVAL_MS;
  }
  else if (led_test_state == LED_TEST_FULL_HOLD)
  {
    interval = LED_TEST_FULL_HOLD_MS;
  }
  else
  {
    interval = LED_TEST_OFF_HOLD_MS;
  }

  if ((now - led_last_step_tick) < interval)
  {
    return;
  }

  if (led_test_state == LED_TEST_FILLING)
  {
    led_lit_count++;
    LED_BuildTestFrame(led_lit_count);

    if (LED_StartTransfer() == HAL_OK)
    {
      led_last_step_tick = now;
      if (led_lit_count >= LED_COUNT)
      {
        led_test_state = LED_TEST_FULL_HOLD;
      }
    }
    else
    {
      led_lit_count--;
    }
  }
  else if (led_test_state == LED_TEST_FULL_HOLD)
  {
    LED_BuildTestFrame(0U);
    if (LED_StartTransfer() == HAL_OK)
    {
      led_last_step_tick = now;
      led_test_state = LED_TEST_OFF_HOLD;
    }
  }
  else
  {
    led_test_color_index++;
    if (led_test_color_index >= (sizeof(led_test_colors) / sizeof(led_test_colors[0])))
    {
      led_test_color_index = 0U;
    }

    led_lit_count = 0U;
    led_test_state = LED_TEST_FILLING;

    /* Let the next main-loop call start the new color immediately. */
    led_last_step_tick = now - LED_TEST_STEP_INTERVAL_MS;
  }
}

void LED_SetBrightness(uint8_t brightness)
{
  led_brightness = brightness;
}

void LED_Clear(void)
{
  if (led_dma_busy != 0U)
  {
    return;
  }

  LED_BuildTestFrame(0U);
  (void)LED_StartTransfer();
}

static uint8_t LED_ApplyBrightness(uint8_t value)
{
  return (uint8_t)((((uint16_t)value * led_brightness) + 127U) / 255U);
}

/**
  * @brief Encode one RGB pixel as 24 PWM values in WS2812B GRB order.
  */
static void LED_EncodePixel(uint32_t pixel_index, LED_ColorTypeDef color)
{
  uint32_t grb;
  uint32_t bit;
  uint32_t buffer_index = pixel_index * WS2812_BITS_PER_LED;

  color.red = LED_ApplyBrightness(color.red);
  color.green = LED_ApplyBrightness(color.green);
  color.blue = LED_ApplyBrightness(color.blue);

  grb = ((uint32_t)color.green << 16U) |
        ((uint32_t)color.red << 8U) |
        (uint32_t)color.blue;

  for (bit = 0U; bit < WS2812_BITS_PER_LED; bit++)
  {
    uint32_t mask = 1UL << (23U - bit);
    led_pwm_buffer[buffer_index + bit] = ((grb & mask) != 0U) ?
                                         WS2812_DUTY_1 : WS2812_DUTY_0;
  }
}

static void LED_AppendResetSlots(void)
{
  uint32_t index = LED_COUNT * WS2812_BITS_PER_LED;

  while (index < WS2812_BUFFER_SIZE)
  {
    led_pwm_buffer[index++] = 0U;
  }
}

/**
  * @brief Build one frame with the first lit_count pixels illuminated.
  */
static void LED_BuildTestFrame(uint32_t lit_count)
{
  LED_ColorTypeDef black = {0U, 0U, 0U};
  uint32_t pixel;

  for (pixel = 0U; pixel < LED_COUNT; pixel++)
  {
    if (pixel < lit_count)
    {
      LED_EncodePixel(pixel, led_test_colors[led_test_color_index]);
    }
    else
    {
      LED_EncodePixel(pixel, black);
    }
  }

  /* 64 low slots hold DIN low for 80 us, allowing the strip to latch. */
  LED_AppendResetSlots();
}

static HAL_StatusTypeDef LED_StartTransfer(void)
{
  HAL_StatusTypeDef status;

  led_dma_busy = 1U;
  __HAL_TIM_SET_COMPARE(&htim3, WS2812_TIM_CHANNEL, 0U);
  __HAL_TIM_SET_COUNTER(&htim3, 0U);

  status = HAL_TIM_PWM_Start_DMA(&htim3,
                                 WS2812_TIM_CHANNEL,
                                 (const uint32_t *)led_pwm_buffer,
                                 (uint16_t)WS2812_BUFFER_SIZE);
  if (status != HAL_OK)
  {
    led_dma_busy = 0U;
  }

  return status;
}

/**
  * @brief Finish the non-blocking transfer started by LED_StartTransfer().
  */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
  if ((htim->Instance == TIM3) &&
      (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3))
  {
    (void)HAL_TIM_PWM_Stop_DMA(htim, WS2812_TIM_CHANNEL);
    __HAL_TIM_SET_COMPARE(htim, WS2812_TIM_CHANNEL, 0U);
    led_dma_busy = 0U;
  }
}
