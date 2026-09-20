#ifndef __LED_H__
#define __LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Adjust these three values when reusing the module with another strip. */
#define LED_COUNT                       10U
#define LED_DEFAULT_BRIGHTNESS          38U
#define LED_RAINBOW_FRAME_INTERVAL_MS   30U

/**
  * @brief Initialize the WS2812B animation state.
  */
void LED_Init(void);

/**
  * @brief Update the moving rainbow when the next frame is due.
  * @note  Call this function repeatedly from the main loop.
  */
void LED_RainbowTask(void);

/**
  * @brief Set global brightness from 0 (off) to 255 (maximum).
  */
void LED_SetBrightness(uint8_t brightness);

/**
  * @brief Turn off all pixels if the DMA channel is currently available.
  */
void LED_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_H__ */
