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

static uint16_t led_pwm_buffer[WS2812_BUFFER_SIZE];
static volatile uint8_t led_dma_busy;
static uint8_t led_brightness;
static uint8_t led_rainbow_phase;
static uint32_t led_last_frame_tick;

static LED_ColorTypeDef LED_ColorWheel(uint8_t position);
static uint8_t LED_ApplyBrightness(uint8_t value);
static void LED_EncodePixel(uint32_t pixel_index, LED_ColorTypeDef color);
static void LED_AppendResetSlots(void);
static void LED_BuildRainbowFrame(void);
static HAL_StatusTypeDef LED_StartTransfer(void);

void LED_Init(void)
{
  led_dma_busy = 0U;
  led_brightness = LED_DEFAULT_BRIGHTNESS;
  led_rainbow_phase = 0U;

  /* Make the first task call generate a frame immediately. */
  led_last_frame_tick = HAL_GetTick() - LED_RAINBOW_FRAME_INTERVAL_MS;
  __HAL_TIM_SET_COMPARE(&htim3, WS2812_TIM_CHANNEL, 0U);
}

void LED_RainbowTask(void)
{
  uint32_t now = HAL_GetTick();

  if (led_dma_busy != 0U)
  {
    return;
  }

  if ((now - led_last_frame_tick) < LED_RAINBOW_FRAME_INTERVAL_MS)
  {
    return;
  }

  LED_BuildRainbowFrame();

  if (LED_StartTransfer() == HAL_OK)
  {
    led_last_frame_tick = now;

    /* Increasing the phase moves a fixed color toward the strip end. */
    led_rainbow_phase++;
  }
}

void LED_SetBrightness(uint8_t brightness)
{
  led_brightness = brightness;
}

void LED_Clear(void)
{
  LED_ColorTypeDef black = {0U, 0U, 0U};
  uint32_t pixel;

  if (led_dma_busy != 0U)
  {
    return;
  }

  for (pixel = 0U; pixel < LED_COUNT; pixel++)
  {
    LED_EncodePixel(pixel, black);
  }

  LED_AppendResetSlots();
  (void)LED_StartTransfer();
}

/**
  * @brief Convert an 8-bit circular position into a full-brightness RGB color.
  * @note  The three sections smoothly interpolate red-green-blue-red.
  */
static LED_ColorTypeDef LED_ColorWheel(uint8_t position)
{
  LED_ColorTypeDef color;
  uint8_t section_position;
  uint8_t rising;
  uint8_t falling;

  if (position < 85U)
  {
    section_position = position;
    rising = (uint8_t)(section_position * 3U);
    falling = (uint8_t)(255U - rising);
    color.red = falling;
    color.green = rising;
    color.blue = 0U;
  }
  else if (position < 170U)
  {
    section_position = (uint8_t)(position - 85U);
    rising = (uint8_t)(section_position * 3U);
    falling = (uint8_t)(255U - rising);
    color.red = 0U;
    color.green = falling;
    color.blue = rising;
  }
  else
  {
    section_position = (uint8_t)(position - 170U);
    rising = (uint8_t)(section_position * 3U);
    falling = (uint8_t)(255U - rising);
    color.red = rising;
    color.green = 0U;
    color.blue = falling;
  }

  return color;
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

static void LED_BuildRainbowFrame(void)
{
  uint32_t pixel;

  for (pixel = 0U; pixel < LED_COUNT; pixel++)
  {
    uint8_t spacing = (uint8_t)(((uint32_t)pixel * 256U) / LED_COUNT);
    uint8_t wheel_position = (uint8_t)(led_rainbow_phase - spacing);
    LED_EncodePixel(pixel, LED_ColorWheel(wheel_position));
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
