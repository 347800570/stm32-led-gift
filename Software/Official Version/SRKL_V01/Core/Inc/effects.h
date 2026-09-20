#ifndef MPLT_EFFECTS_H
#define MPLT_EFFECTS_H
#include "led.h"
/* 8号模式：熄灭阈值比点亮阈值低一层跨度的25%，可按实物调节。 */
#define VOLUME_HYSTERESIS_PERCENT 25U
#define VOLUME_HOLD_BRIGHTNESS 64U
#define VOLUME_FADE_MS 80U
void Effects_Reset(void);
void Effects_Render(uint8_t mode, uint8_t speed, uint32_t elapsed_ms,
                    LED_Color *pixels);
#endif
