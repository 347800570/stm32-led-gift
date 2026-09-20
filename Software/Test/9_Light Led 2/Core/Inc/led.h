#ifndef __LED_H__
#define __LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Change only this value when testing a strip with a different LED count. */
#define LED_COUNT                       74U

#define LED_TEST_BRIGHTNESS             32U
#define LED_TEST_STEP_INTERVAL_MS       50U
#define LED_TEST_FULL_HOLD_MS           500U
#define LED_TEST_OFF_HOLD_MS            300U

/**
  * @brief Initialize the WS2812B continuity test state.
  */
void LED_Init(void);

/**
  * @brief Run the red, green and blue progressive-fill continuity test.
  * @note  Call this function repeatedly from the main loop.
  */
void LED_ContinuityTestTask(void);

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
