#ifndef MPLT_BEAT_H
#define MPLT_BEAT_H
#include <stdint.h>
/* 8 kHz下的定点滤波系数：40 Hz高通、两个约400 Hz低通，均为一阶。 */
#define BEAT_HP_COEFF_Q8 8
#define BEAT_LP_COEFF_Q8 69
#define BEAT_REFRACTORY_MS 150U
#define BEAT_PULSE_MS 150U
#define BEAT_WARMUP_BLOCKS 16U
void Beat_Reset(void);
/* 每个有效块只归入一种结果；预热块不参与统计。 */
enum { BEAT_HIT, BEAT_BELOW, BEAT_NO_RISE, BEAT_NOT_ARMED, BEAT_LOCKED, BEAT_RESULT_COUNT };
typedef struct {
    uint32_t peak, threshold, background;
    uint16_t results[BEAT_RESULT_COUNT];
    uint8_t sensitivity, valid;
} Beat_Diagnostics;
void Beat_TakeDiagnostics(Beat_Diagnostics *out);
void Beat_PushSample(int32_t sample);
void Beat_EndBlock(uint8_t sensitivity, uint32_t now);
void Beat_ClearPulse(void);
uint8_t Beat_PulseLevel(void);
uint32_t Beat_Energy(void);
uint32_t Beat_Background(void);
uint32_t Beat_Count(void);
#endif
