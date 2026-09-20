#ifndef MPLT_LED_LAYOUT_H
#define MPLT_LED_LAYOUT_H
#include "led.h"
#define LAYOUT_WIDTH 27U
#define LAYOUT_HEIGHT 8U
typedef struct { uint8_t x, y; } LED_Position;
/* 数组下标 = 灯珠编号 - 1；y=0 为木板最底部。常量存放在 Flash。 */
extern const LED_Position led_positions[LED_COUNT];
#endif
