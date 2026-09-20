#ifndef MPLT_LED_H
#define MPLT_LED_H
#include <stdint.h>

/* 修改灯珠数量只需调整这里；增大数量后必须重新检查 RAM。 */
#define LED_COUNT 67U
#define LED_MAX_BRIGHTNESS 30U
typedef struct { uint8_t r, g, b; } LED_Color;
void LED_Init(void);
uint8_t LED_Ready(void);
uint8_t LED_Show(const LED_Color *pixels, uint8_t brightness);
uint32_t LED_ErrorCount(void);
#endif
